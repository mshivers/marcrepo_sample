#pragma once
#include "accumulators.h"
#include "ema.h"
#include "graph.h"
#include "iceberg.h"
#include "market_data.h" 
#include "serialize.h" 
#include "strategy.h" 
#include "util_nodes.h" 

// Calculates the DecayedSum of signed size refreshed, multiplied by the current trade direction
struct AccumRefreshed : public ValueNode {
    void compute() override {
        value_ = recent_refreshes_->value() * trade_direction_->value();
        status_ = StatusCode::OK;
    }

    SERIALIZE(AccumRefreshed, market_data_, length_in_nanos_)
    
    std::chrono::nanoseconds length_in_nanos_;
    ValueNode* recent_refreshes_;
    ValueNode* trade_direction_;
    MarketData* market_data_;
    
    protected:
    AccumRefreshed(Graph* g, MarketData* market_data, std::chrono::nanoseconds length_in_nanos)
        : ValueNode(g, Units::SIZE),  
          length_in_nanos_(length_in_nanos),
          market_data_(market_data) {
        auto size_refreshed = g->add<SizeRefreshed>(market_data->symbol());
        trade_direction_ = g->add<TradeDirection>(market_data);
        recent_refreshes_ = g->add<TimeDecayedSum>(size_refreshed, size_refreshed->getClock(), length_in_nanos);
        setParents(recent_refreshes_, trade_direction_);
        setClock(g->add<OnTrade>(market_data));
    }
};

// Returns the distance between the trade price and the input theo just prior to the trade
struct SignedTradeCost : public ValueNode {
    void compute() override {
        value_ = trade_size_->value() * ( trade_price_->value() - last_theo_->heldValue() );
        status_ = StatusCode::OK;
    }

    SERIALIZE(SignedTradeCost, theo_)
    
    Theo* theo_;
    ValueNode* trade_price_;
    ValueNode* trade_size_;
    ValueNode* last_theo_;
    
    protected:
    SignedTradeCost(Graph* g, Theo* theo)
        : ValueNode(g, Units::NONE),  
          theo_(theo) {
        trade_price_ = g->add<AvgTradePrice>(theo->marketData());
        trade_size_ = g->add<TradeSize>(theo->marketData());
        last_theo_ = g->add<Last>(theo); 
        setParents(trade_price_, trade_size_, last_theo_);
        setClock(g->add<OnTrade>(theo->marketData()));
    }
};
    
//TODO(mshivers): write a version of TradeAggression that compares the trade price to the theo *after* a small delay, to give other in-flight orders
//a chance to hit the market.  That way, orders that are just responding to a related-market change don't seem to be overly aggressive when they are
//the first order to arrive.  
//TODO(mshivers): write a TradeAggression for each Side, and construct and AggressionBias signal that is the difference between Ask and Bid aggression.
struct TradeAggression : public ValueNode {
    void compute() override {
        //we remove current dist from accum, so it's just decayed previous accum
        value_ = last_recent_cost_->value();
        if ( trade_direction_->heldValue() < 0 )
            value_ *= -1;
        status_ = StatusCode::OK;
    }

    SERIALIZE(TradeAggression, theo_, length_in_nanos_)
    
    Theo* theo_;
    std::chrono::nanoseconds length_in_nanos_;
    ValueNode* last_recent_cost_;
    ValueNode* trade_direction_;
    
    protected:
    TradeAggression(Graph* g, Theo* theo, std::chrono::nanoseconds length_in_nanos)
        : ValueNode(g, Units::NONE),  
          theo_(theo),
          length_in_nanos_(length_in_nanos) {
        auto market_data = theo->marketData();
        trade_direction_ = g->add<TradeDirection>(market_data);
        auto signed_trade_cost = g->add<SignedTradeCost>(theo);
        auto padded_signed_trade_cost = g->add<Pad>(signed_trade_cost, market_data, 0); 
        auto recent_cost = g->add<TimeDecayedSum>(padded_signed_trade_cost, market_data, length_in_nanos);
        last_recent_cost_ = g->add<Last>(recent_cost);
        setParents(trade_direction_, last_recent_cost_);
        setClock(market_data);
    }
};

//TradeAggression that ticks on all updates for either theo, and accumulates the trade
//cost for both theos together and signs it with the trade sign of the most-recently traded theo.
struct JointTradeAggression : public ValueNode {
    void compute() override {
        value_ = trade_direction_->heldValue() * recent_cost_->value(); 
        status_ = StatusCode::OK;
    }

    SERIALIZE(JointTradeAggression, theos_, length_in_nanos_)
    
    std::vector<Theo*> theos_;
    std::chrono::nanoseconds length_in_nanos_;
    ValueNode* recent_cost_;
    ValueNode* trade_direction_;

    protected:
    JointTradeAggression(Graph* g, std::vector<Theo*> theos, std::chrono::nanoseconds length_in_nanos)
        : ValueNode(g, Units::NONE),  
          theos_(theos),
          length_in_nanos_(length_in_nanos) {
        std::vector<ValueNode*> signed_trade_cost_vec;
        std::vector<ValueNode*> trade_direction_vec;
        std::vector<ClockNode*> update_clock_vec;
        for ( auto theo : theos ) {
            update_clock_vec.emplace_back(theo->marketData());
            signed_trade_cost_vec.emplace_back(g->add<SignedTradeCost>(theo));
            trade_direction_vec.emplace_back(g->add<TradeDirection>(theo->marketData()));
        }
        auto update_clock = joinClocks(update_clock_vec);
        auto signed_trade_cost = g->add<Join>(signed_trade_cost_vec);
        auto padded_signed_trade_cost_ = g->add<Pad>(signed_trade_cost, update_clock, 0); 
        trade_direction_ = g->add<Join>(trade_direction_vec);
        recent_cost_= g->add<TimeDecayedSum>(padded_signed_trade_cost_, update_clock, length_in_nanos);
        setParents(trade_direction_, recent_cost_);
        setClock(update_clock);
    }
};
  
// Cov of TradeCost between traded_sym and ref_sym, calculated when traded_sym trades, so if ref is leading, 
// this will be positive.  If traded_sym is leading, this will be ~0.
struct TradeCostLeadingCov : public ValueNode {
    void compute() override {
        double update = traded_cost_->value() * ref_cost_->heldValue();
        cov_ema_.updateEMA(update);
        traded_var_ema_.updateEMA(std::pow(traded_cost_->value(), 2));
        ref_var_ema_.updateEMA(std::pow(ref_cost_->heldValue(), 2));
        double denom = std::max(pow(traded_var_ema_.value() * ref_var_ema_.value(), 0.5), 1e-12);
        value_ = cov_ema_.value()/denom;
        status_ = StatusCode::OK;
    }

    std::string defaultName() const override { 
        auto sym_string = getShortSymbol(traded_theo_->symbol()); 
        auto ref_string = getShortSymbol(ref_theo_->symbol()); 
        return (getClassName() + sym_string + ref_string 
              + getDurationString(cost_length_in_nanos_) 
              + std::to_string((int)corr_decay_length_) + "t");
    }

    SERIALIZE(TradeCostLeadingCov, traded_theo_, ref_theo_, cost_length_in_nanos_, corr_decay_length_);
    
    Theo* traded_theo_;
    Theo* ref_theo_;
    std::chrono::nanoseconds cost_length_in_nanos_;
    double corr_decay_length_;
    ValueNode* traded_cost_;
    ValueNode* ref_cost_;
    SimpleEMA cov_ema_, traded_var_ema_, ref_var_ema_;


    protected:
    TradeCostLeadingCov(Graph* g, Theo* traded_theo, Theo* ref_theo, 
                 std::chrono::nanoseconds cost_length_in_nanos,
                 double corr_decay_length) 
        : ValueNode(g, Units::NONE),  
          traded_theo_(traded_theo),
          ref_theo_(ref_theo),
          cost_length_in_nanos_(cost_length_in_nanos),
          corr_decay_length_(corr_decay_length) {
        value_ = 0;

        auto traded_md = traded_theo->marketData();
        auto ref_md = ref_theo->marketData();
        auto joint_clock = joinClocks(traded_md, ref_md);

        auto traded_signed_trade_cost = g->add<SignedTradeCost>(traded_theo);
        auto traded_padded_signed_trade_cost = g->add<Pad>(traded_signed_trade_cost, joint_clock, 0); 
        traded_cost_ = g->add<TimeDecayedSum>(traded_padded_signed_trade_cost, joint_clock, cost_length_in_nanos);
 
        auto ref_signed_trade_cost = g->add<SignedTradeCost>(ref_theo);
        auto ref_padded_signed_trade_cost = g->add<Pad>(ref_signed_trade_cost, joint_clock, 0); 
        ref_cost_ = g->add<TimeDecayedSum>(ref_padded_signed_trade_cost, joint_clock, cost_length_in_nanos);

        cov_ema_.setLength(corr_decay_length);
        traded_var_ema_.setLength(corr_decay_length);
        ref_var_ema_.setLength(corr_decay_length);

        setParents(traded_cost_, ref_cost_);
        setClock(g->add<OnTrade>(traded_md));
    }
};
 
struct AbsoluteVariation : public ValueNode {
    void updateValue() {
        value_ += std::abs(node_->value() - lag_node_value_); 
        lag_node_value_ = node_->value();
    }

    void decayValue() {
        Int64 current_time = getGraph()->nSecUptime(); 
        double elapsed_nanos = current_time - last_decay_time_;
        if ( elapsed_nanos < 1 ) elapsed_nanos = 1;
        double decay_pct = std::max(0.0, 1.0 - elapsed_nanos / length_in_nanos_.count());
        value_ *= decay_pct;
        last_decay_time_ = getGraph()->nSecUptime(); 
    }

    void compute() override {
        if ( unlikely(status_==StatusCode::INIT) ) {
            last_decay_time_ = getGraph()->nSecUptime(); 
            lag_node_value_ = node_->value();
        } else {
            decayValue();
            updateValue();
        }
        status_ = StatusCode::OK;
    }

    std::string defaultName() const override { 
        return getClassName() + node_->getName() + getDurationString(length_in_nanos_);
    }

    SERIALIZE(AbsoluteVariation, node_, length_in_nanos_);
    
    ValueNode* node_;
    std::chrono::nanoseconds length_in_nanos_;
    Int64 last_decay_time_;
    double lag_node_value_;

    protected:
    AbsoluteVariation(Graph* g, ValueNode* node, std::chrono::nanoseconds length_in_nanos) 
        : ValueNode(g, Units::NONE),  
          node_(node),
          length_in_nanos_(length_in_nanos) {
        value_ = 0;

        setParents(node_);
        setClock(node_);
    }
};
  
struct Latency : public ValueNode {
    Int64 exchangeLatency() {
        auto source = graph_->currentSource();
        auto market_data = dynamic_cast<const MarketDataSource*>(source);
        assert(market_data);
        auto elapsed = market_data->receiveTimestamp() - market_data->exchangeTimestamp();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count(); 
    }

    void compute() override {
        value_ =  exchangeLatency();
        status_ = StatusCode::OK;
    }

    std::string defaultName() const override { 
        std::string syms;
        for (auto sym : symbols_)
            syms += getShortSymbol(sym);
        return (getClassName() + syms);
    }

    SERIALIZE(Latency, symbols_);

    std::vector<std::string> symbols_;

    Latency(Graph* g, std::vector<std::string> symbols)
        : ValueNode(g),
          symbols_(symbols){

        std::vector<ClockNode*> mds;
        for ( auto sym : symbols_ )
            mds.emplace_back(g->add<RawMarketData>(sym));
        setClock(mds);
    }
};

struct RelativePacketRate: public ValueNode {
    vplat_clock::time_point currentExchangeTime() {
        return getGraph()->getStrategy()->exchangeTimestamp();
    }

    void compute() override {
        if ( unlikely(status_==StatusCode::INIT) ) {
            last_exchange_time_ = currentExchangeTime();
        } else {
            auto current_exchange_time_ = currentExchangeTime();
            auto elapsed = current_exchange_time_ - last_exchange_time_;
            double nanos_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count(); 
            double decay_factor = std::max(0.0, 1.0 - nanos_elapsed/ems_length_.count());

            base_ems_ *= decay_factor;
            ref_ems_ *= decay_factor;
            if (market_data_->ticked())
                base_ems_ += 1; 
            else
                ref_ems_ += 1;

            last_exchange_time_ = current_exchange_time_;
        }
        double denom = base_ems_ + ref_ems_;
        if (denom > 0)
            value_ = base_ems_ / denom;
        else
            value_ = 1.0 / (1.0 + ref_symbols_.size());
        status_ = StatusCode::OK;
    }

    std::string defaultName() const override { 
        std::string ref_syms;
        for (auto ref_sym : ref_symbols_)
            ref_syms += getShortSymbol(ref_sym);
        return (getClassName() + market_data_->shortSymbol() + ref_syms + getDurationString(ems_length_));
    }

    SERIALIZE(RelativePacketRate, market_data_, ref_symbols_, ems_length_);

    std::vector<std::string> ref_symbols_;
    std::chrono::nanoseconds ems_length_;
    double base_ems_{0}, ref_ems_{0};
    vplat_clock::time_point last_exchange_time_;
    MarketData* market_data_;
    
    RelativePacketRate(Graph* g, MarketData* market_data,
                       std::vector<std::string> ref_symbols, 
                       std::chrono::nanoseconds ems_length)
        : ValueNode(g, Units::NONE),
          ref_symbols_(ref_symbols),
          ems_length_(ems_length),
          market_data_(market_data) {

        {
            std::set<std::string> exchanges;
            exchanges.insert(getExch(market_data->symbol()));
            for ( auto ref_sym : ref_symbols ) 
                exchanges.insert(getExch(ref_sym));

            if(exchanges.size() > 1) for (auto e : exchanges) std::cout<<"Exch: " << e << std::endl;
            assert(exchanges.size() == 1);
        }

        std::vector<ClockNode*> ref_mds;
        for ( auto ref_sym : ref_symbols )
            ref_mds.emplace_back(g->add<RawMarketData>(ref_sym));

        setClock(market_data, ref_mds);
    }
};
 
struct PacketRate: public ValueNode {
    vplat_clock::time_point currentExchangeTime() {
        return getGraph()->getStrategy()->exchangeTimestamp();
    }

    void compute() override {
        if ( unlikely(status_==StatusCode::INIT) ) {
            last_exchange_time_ = currentExchangeTime();
        } else {
            auto current_exchange_time = currentExchangeTime();
            auto elapsed = current_exchange_time - last_exchange_time_;
            double nanos_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count(); 
            if (nanos_elapsed>0) {
                last_exchange_time_ = current_exchange_time;
                value_ *= std::max(0.0, 1.0 - nanos_elapsed/ems_length_.count());
            }
            if (market_data_->ticked()) value_ += 1;
        }
        status_ = StatusCode::OK;
    }

    std::string defaultName() const override { 
        return (getClassName() + market_data_->shortSymbol() + getDurationString(ems_length_));
    }

    SERIALIZE(PacketRate, market_data_, decay_clock_, ems_length_);

    ClockNode* decay_clock_;
    std::chrono::nanoseconds ems_length_;
    vplat_clock::time_point last_exchange_time_;
    MarketData* market_data_;
    
    PacketRate(Graph* g, MarketData* market_data, ClockNode* decay_clock, std::chrono::nanoseconds ems_length)
        : ValueNode(g, Units::NONE),
          decay_clock_(decay_clock),
          ems_length_(ems_length),
          market_data_(market_data) {
        setClock(market_data, decay_clock);
    }
};

//TODO(mshivers): Write a Hayashi Yoshida covariance node with Tick decay.  Tick is sometimes significant when Time
//isn't
struct HYTimeCov : public ValueNode {
    void updateValue() {
        if ( sig1_->ticked() ) {
            dx1_ = sig1_->value() - lag1_;
            if ( last_ticked_!=sig1_ ) {
                last_ticked_=sig1_;
                value_ += dx1_ * dx2_;
                lag1_ = sig1_->value();  //this is different from the classical HY formula, but works better.
            }
        } else if ( sig2_->ticked() ) {
            dx2_ = sig2_->value() - lag2_;
            if ( last_ticked_!=sig2_ ) {
                last_ticked_=sig2_;
                value_ += dx1_ * dx2_;
                lag2_ = sig2_->value();
            }
        }
    }

    void decayValue() {
        Int64 current_time = getGraph()->nSecUptime(); 
        double elapsed_nanos = current_time - last_decay_time_;
        if ( elapsed_nanos < 1 ) elapsed_nanos = 1;
        double decay_pct = std::max(0.0, 1.0 - elapsed_nanos / length_in_nanos_.count());
        value_ *= decay_pct;
        last_decay_time_ = getGraph()->nSecUptime(); 
    }

    void compute() override {
        if ( unlikely(status_==StatusCode::INIT) ) {
            lag1_ = sig1_->heldValue();
            lag2_ = sig2_->heldValue();
            last_ticked_ = sig1_; //arbitrary
            last_decay_time_ = getGraph()->nSecUptime(); 
        } else {
            decayValue();
            updateValue();
        }
        status_ = StatusCode::OK;
    }

    std::string defaultName() const override { 
        return ( getClassName() + sig1_->defaultName() + sig2_->defaultName() 
               + getDurationString(length_in_nanos_) ); 
    }

    SERIALIZE(HYTimeCov, sig1_, sig2_, length_in_nanos_, decay_clock_);
    
    ValueNode *sig1_, *sig2_, *last_ticked_;
    std::chrono::nanoseconds length_in_nanos_;
    ClockNode* decay_clock_;
    Int64 last_decay_time_;
    double lag1_, lag2_;
    double dx1_{0}, dx2_{0};

    protected:
    HYTimeCov(Graph* g, ValueNode* sig1, ValueNode* sig2, 
        std::chrono::nanoseconds length_in_nanos, ClockNode* decay_clock) 
        : ValueNode(g, Units::NONE),
          sig1_(sig1),
          sig2_(sig2),
          length_in_nanos_(length_in_nanos),
          decay_clock_(decay_clock){
        assert(!hasCommonSourceClock(sig1, sig2));  //calculations assume strictly asyncronous.
        value_ = 0;
        setParents(sig1, sig2);
        setClock(sig1, sig2, decay_clock);
    }
};

struct QuadraticVariation : public ValueNode {
    void updateValue() {
        dx_ = sig_->heldValue() - lag_;
        value_ += dx_ * dx_;
        lag_ = sig_->heldValue();
    }

    void decayValue() {
        Int64 current_time = getGraph()->nSecUptime(); 
        double elapsed_nanos = current_time - last_decay_time_;
        if ( elapsed_nanos < 1 ) elapsed_nanos = 1;
        double decay_pct = std::max(0.0, 1.0 - elapsed_nanos / length_in_nanos_.count());
        value_ *= decay_pct;
        last_decay_time_ = getGraph()->nSecUptime(); 
    }

    void compute() override {
        if ( unlikely(status_==StatusCode::INIT) ) {
            lag_ = sig_->heldValue();
            last_decay_time_ = getGraph()->nSecUptime(); 
        } else {
            if ( decay_clock_->ticked() ) decayValue();
            if ( sig_->ticked() ) updateValue();
        }
        status_ = StatusCode::OK;
    }

    std::string defaultName() const override { 
        return (getClassName() + sig_->defaultName() + getDurationString(length_in_nanos_)); 
    }

    SERIALIZE(QuadraticVariation, sig_, length_in_nanos_, decay_clock_);
    
    ValueNode* sig_;
    std::chrono::nanoseconds length_in_nanos_;
    ClockNode* decay_clock_;
    Int64 last_decay_time_;
    double lag_;
    double dx_;

    protected:
    QuadraticVariation(Graph* g, ValueNode* sig, std::chrono::nanoseconds length_in_nanos, ClockNode* decay_clock) 
        : ValueNode(g, Units::NONE),
          sig_(sig),
          length_in_nanos_(length_in_nanos),
          decay_clock_(decay_clock){
        value_ = 0;
        setParents(sig);
        setClock(sig, decay_clock);
    }
};


struct VWAPCov : public ValueNode {

    void decayValue() {
        Int64 current_time = getGraph()->nSecUptime(); 
        double elapsed_nanos = current_time - last_decay_time_;
        if ( elapsed_nanos < 1 ) elapsed_nanos = 1;
        double decay_pct = std::max(0.0, 1.0 - elapsed_nanos / cov_decay_length_.count());
        value_ *= decay_pct;
        last_decay_time_ = getGraph()->nSecUptime(); 
    }

    void compute() override {
        if ( unlikely(status_==StatusCode::INIT) ) {
            last_decay_time_ = getGraph()->nSecUptime(); 
            value_ = 0;
        } else {
            decayValue();
            auto base_delta = (base_theo_->heldValue() - base_vwap_->heldValue());
            auto ref_delta = (ref_theo_->heldValue() - ref_vwap_->heldValue());
            value_ += base_delta * ref_delta; 
        }
        status_ = StatusCode::OK;
    }

    std::string defaultName() const override { 
        return (getClassName() + base_theo_->defaultName() + ref_theo_->defaultName() 
              + "VWAP" + getDurationString(nano_vwap_length_) 
              + "Cov" + getDurationString(cov_decay_length_) );
    }

    SERIALIZE(VWAPCov, base_theo_, ref_theo_, nano_vwap_length_, cov_decay_length_);

    Theo* base_theo_;
    Theo* ref_theo_;
    ValueNode* ref_vwap_;
    ValueNode* base_vwap_;
    std::chrono::nanoseconds nano_vwap_length_;
    std::chrono::nanoseconds cov_decay_length_;
    Int64 last_decay_time_;

    protected:
    VWAPCov(Graph* g, Theo* base_theo, Theo* ref_theo, 
            std::chrono::nanoseconds nano_vwap_length,
            std::chrono::nanoseconds cov_decay_length) 
        : ValueNode(g, Units::NONE),  
          base_theo_(base_theo), 
          ref_theo_(ref_theo), 
          nano_vwap_length_(nano_vwap_length),
          cov_decay_length_(cov_decay_length) {
        base_vwap_ = g->add<TimeVWAP>(base_theo->marketData(), nano_vwap_length);
        ref_vwap_ = g->add<TimeVWAP>(ref_theo->marketData(), nano_vwap_length);
        setParents(base_vwap_, ref_vwap_, base_theo, ref_theo);
        setClock(base_vwap_, ref_vwap_, base_theo, ref_theo);
    }
};


//TODO(mshivers): for BTEC, for iceberg refreshes, an order can be price modified to a very deep price point, instead
//of cancelling it, then repriced to BBO to activate it again, so the total fill on that order could be arbitrarily large.
//This might make the refreshMult on the iceberg split book mean something different.  Write state nodes for btec that 
//look at both the total size filled on the existing orders and the number of times it was price-modified to see if 
//a tree wants to split on those data.


