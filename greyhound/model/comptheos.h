#pragma once
#include <cmath>
#include <chrono>

#include "clocks.h"
#include "ema.h"
#include "graph.h"
#include "market_data.h"
#include "math_nodes.h"
#include "serialize.h"
#include "theos.h"
#include "util_nodes.h"

using seconds = std::chrono::seconds;

// Basic theoretical price signal implementations.
struct TimeCompTheo : public Theo {
    void compute() override {
        if ( ref_theo_->ticked() or ref_ema_->ticked() ) {
            auto ref_ratio = ref_theo_->heldValue() / ref_ema_->heldValue();
            ref_mult_ = std::pow(ref_ratio, vol_mult_);
        }
        value_ = base_ema_->heldValue() * ref_mult_;
        status_ = StatusCode::OK;
    }

    std::string defaultName() const override { 
        return (getClassName() + market_data_->shortSymbol() + ref_theo_->getName() 
              + getDurationString(ema_length_)
              + std::to_string((int)(100*vol_mult_)) + "vm");
    }

    SERIALIZE(TimeCompTheo, base_theo_, ref_theo_, ema_length_, vol_mult_);

    Theo* base_theo_;
    Theo* ref_theo_;
    ValueNode* base_ema_;
    ValueNode* ref_ema_;
    std::chrono::nanoseconds ema_length_;
    double vol_mult_;
    double ref_mult_{1.0};

    TimeCompTheo(Graph* g, Theo* base_theo, Theo* ref_theo, 
                  std::chrono::nanoseconds ema_length, double vol_mult)
        : Theo(g, base_theo->marketData()),
          base_theo_(base_theo),
          ref_theo_(ref_theo),
          ema_length_(ema_length),
          vol_mult_(vol_mult) {

        base_ema_ = g->add<TimeEMA>(base_theo, base_theo->marketData(), ema_length);
        ref_ema_ = g->add<TimeEMA>(ref_theo, ref_theo->marketData(), ema_length);

        setParents(base_ema_, ref_ema_, ref_theo_);
        setClock(base_ema_, ref_ema_, ref_theo_);
    }
};

//theo emas decay on a join of both theo clocks by fixed amount 
struct TickCompTheo : public Theo {
    void compute() override {
        if ( ref_theo_->ticked() or ref_ema_->ticked() ) {
            auto ref_ratio = ref_theo_->heldValue() / ref_ema_->heldValue();
            ref_mult_ = std::pow(ref_ratio, vol_mult_);
        }
        value_ = base_ema_->heldValue() * ref_mult_;
        status_ = StatusCode::OK;
    }

    std::string defaultName() const override { 
        return (getClassName() + shortSymbol() + ref_theo_->getName() 
              + std::to_string((int)ema_length_) +"t"
              + std::to_string((int)(100*vol_mult_)) + "vm");
    }

    SERIALIZE(TickCompTheo, base_theo_, ref_theo_, ema_length_, vol_mult_);

    Theo* base_theo_;
    Theo* ref_theo_;
    ValueNode* base_ema_;
    ValueNode* ref_ema_;
    double ema_length_;
    double vol_mult_;
    double ref_mult_{1.0};

    TickCompTheo(Graph* g, Theo* base_theo, Theo* ref_theo, 
                  double ema_length, double vol_mult)
        : Theo(g, base_theo->marketData()),
          base_theo_(base_theo),
          ref_theo_(ref_theo),
          ema_length_(ema_length),
          vol_mult_(vol_mult) {

        auto joint_clock = joinClocks(base_theo, ref_theo);
        base_ema_ = g->add<TickEMA>(base_theo, joint_clock, ema_length);
        ref_ema_ = g->add<TickEMA>(ref_theo, joint_clock, ema_length);

        setParents(base_ema_, ref_ema_, ref_theo_);
        setClock(joint_clock);
    }
};


struct TimeVWAPCompTheo : public Theo {
    void compute() override {
        if ( ref_theo_->ticked() or ref_vwap_->ticked() ) {
            auto ref_ratio = ref_theo_->heldValue() / ref_vwap_->heldValue();
            ref_mult_ = std::pow(ref_ratio, vol_mult_);
        }
        value_ = base_vwap_->heldValue() * ref_mult_;
        status_ = StatusCode::OK;
    }

    std::string defaultName() const override { 
        return (getClassName() + shortSymbol() + ref_theo_->getName() 
              + getDurationString(nano_vwap_length_) 
              + std::to_string((int)(100*vol_mult_)) + "vm");
    }

    SERIALIZE(TimeVWAPCompTheo, market_data_, ref_theo_, nano_vwap_length_, vol_mult_);

    Theo* ref_theo_;
    ValueNode* ref_vwap_;
    ValueNode* base_vwap_;
    std::chrono::nanoseconds nano_vwap_length_;
    double vol_mult_;
    double ref_mult_{1.0};

    protected:
    TimeVWAPCompTheo(Graph* g, MarketData* base_market_data, Theo* ref_theo, 
            std::chrono::nanoseconds nano_vwap_length, double vol_mult) 
    : Theo(g, base_market_data), 
      ref_theo_(ref_theo), 
      nano_vwap_length_(nano_vwap_length), 
      vol_mult_(vol_mult) {

        auto on_base_trades = g->add<OnTrade>(base_market_data);
        auto on_ref_trades = g->add<OnTrade>(ref_theo->marketData());
        base_vwap_ = g->add<TimeVWAP>(base_market_data, nano_vwap_length);
        ref_vwap_ = g->add<TimeVWAP>(ref_theo->marketData(), nano_vwap_length);

        setParents(base_vwap_, ref_vwap_, ref_theo);
        setClock(on_base_trades, on_ref_trades, ref_theo);
    }
};


struct TickVWAPCompTheo : public Theo {
    void compute() override {
        if ( ref_theo_->ticked() or ref_vwap_->ticked() ) {
            auto ref_ratio = ref_theo_->heldValue() / ref_vwap_->heldValue();
            ref_mult_ = std::pow(ref_ratio, vol_mult_);
        }
        value_ = base_vwap_->heldValue() * ref_mult_;
        status_ = StatusCode::OK;
    }

    std::string defaultName() const override { 
        return (getClassName() + shortSymbol() + ref_theo_->getName() 
              + std::to_string((int)tick_vwap_length_) +"t"
              + std::to_string((int)(100*vol_mult_)) + "vm");
    }

    SERIALIZE(TickVWAPCompTheo, market_data_, ref_theo_, tick_vwap_length_, vol_mult_);

    Theo* ref_theo_;
    ValueNode* ref_vwap_;
    ValueNode* base_vwap_;
    double tick_vwap_length_;
    double vol_mult_;
    double ref_mult_{1.0};

    protected:
    TickVWAPCompTheo(Graph* g, MarketData* base_market_data, Theo* ref_theo, 
            double tick_vwap_length, double vol_mult) 
    : Theo(g, base_market_data), 
      ref_theo_(ref_theo), 
      tick_vwap_length_(tick_vwap_length), 
      vol_mult_(vol_mult) {

        auto on_base_trades = g->add<OnTrade>(base_market_data);
        auto on_ref_trades = g->add<OnTrade>(ref_theo->marketData());
        base_vwap_ = g->add<TickVWAP>(base_market_data, on_base_trades, tick_vwap_length);
        ref_vwap_ = g->add<TickVWAP>(ref_theo->marketData(), on_ref_trades, tick_vwap_length);

        setParents(base_vwap_, ref_vwap_, ref_theo);
        setClock(on_base_trades, on_ref_trades, ref_theo);
    }
};


//Ref Trade Intensity is proportional to the decay legnth for emas in the TICT
//node, so TICT decays quickly (short ema length) when the base security
//trades, and decays slowly (long ema length) when the ref security trades
struct RefTradeIntensity : public ValueNode {
    void compute() override {
        if ( base_long_sum_->heldValue() >= 1 )
            base_intensity_ = base_short_sum_->heldValue() / base_long_sum_->heldValue();
        else
            base_intensity_ = base_short_sum_->heldValue();

        if ( ref_long_sum_->heldValue() >= 1 )
            ref_intensity_ = ref_short_sum_->heldValue() / ref_long_sum_->heldValue();
        else
            ref_intensity_ = ref_short_sum_->heldValue();

        if ( ( base_intensity_ > 0 ) or ( ref_intensity_ > 0 ) ) 
            value_ = ref_intensity_ / ( base_intensity_ + ref_intensity_ );
        else
            value_ = 0.5;
        status_ = StatusCode::OK;
    }

    SERIALIZE(RefTradeIntensity, base_md_, ref_md_, long_decay_, short_decay_);
    
    MarketData* base_md_;
    MarketData* ref_md_;
    std::chrono::nanoseconds long_decay_; 
    std::chrono::nanoseconds short_decay_;
    TimeDecayedSum* base_long_sum_;
    TimeDecayedSum* base_short_sum_;
    TimeDecayedSum* ref_long_sum_;
    TimeDecayedSum* ref_short_sum_;
    double base_intensity_{0}, ref_intensity_{0};

    protected:
    RefTradeIntensity(Graph* g, MarketData* base_md, 
            MarketData* ref_md,
            std::chrono::nanoseconds long_decay,
            std::chrono::nanoseconds short_decay)
        : ValueNode(g, Units::NONE),
          base_md_(base_md),
          ref_md_(ref_md),
          long_decay_(long_decay), 
          short_decay_(short_decay) {
        assert(short_decay < long_decay);
        value_ = 0;
        auto on_base_trades = g->add<OnTrade>(base_md); 
        auto on_ref_trades = g->add<OnTrade>(ref_md); 
        auto joint_clock = joinClocks(on_base_trades, on_ref_trades);

        auto base_trade_size = g->add<TradeSize>(base_md);
        auto padded_base_trades = g->add<Pad>(base_trade_size, joint_clock, 0);

        base_long_sum_ = g->add<TimeDecayedSum>(padded_base_trades, 
                joint_clock, long_decay);
        base_short_sum_ = g->add<TimeDecayedSum>(padded_base_trades, 
                joint_clock, short_decay);

        auto ref_trade_size = g->add<TradeSize>(ref_md);
        auto padded_ref_trades = g->add<Pad>(ref_trade_size, joint_clock, 0);
        ref_long_sum_ = g->add<TimeDecayedSum>(padded_ref_trades, joint_clock, 
                long_decay);
        ref_short_sum_ = g->add<TimeDecayedSum>(padded_ref_trades, joint_clock, 
                short_decay);

        setParents(base_long_sum_, base_short_sum_, ref_long_sum_, ref_short_sum_);
        setClock(joint_clock);
    }
};

struct TradeIntensityCompTheo : public Theo {
    using nanos = std::chrono::nanoseconds;
    void compute() override {
        //Use UpdateEMA function to update base and ref ema values locally.
        if ( ref_theo_->ticked() or ref_ema_->ticked() ) {
            auto ref_ratio = ref_theo_->heldValue() / ref_ema_->heldValue();
            ref_mult_ = std::pow(ref_ratio, vol_mult_);
        }
        value_ = base_ema_->heldValue() * ref_mult_;
        status_ = StatusCode::OK;
    }

    std::string defaultName() const override { 
        return ("TICT" + shortSymbol() + ref_theo_->getName()
              + getDurationString(long_decay_) + getDurationString(short_decay_)
              + std::to_string((int)intensity_mult_) +"t"
              + std::to_string((int)(100*vol_mult_)) + "vm");
    }

    SERIALIZE(TradeIntensityCompTheo, base_theo_, ref_theo_, long_decay_, 
            short_decay_, intensity_mult_, vol_mult_);

    Theo* base_theo_;
    Theo* ref_theo_;
    ValueNode* base_ema_;
    ValueNode* ref_ema_;
    nanos long_decay_, short_decay_;
    double intensity_mult_;
    double vol_mult_;
    double ref_mult_{1.0};

    protected:
    TradeIntensityCompTheo(Graph* g, Theo* base_theo, Theo* ref_theo, 
            nanos long_decay, nanos short_decay, double intensity_mult, 
            double vol_mult)
        : Theo(g, base_theo->marketData()),
          base_theo_(base_theo),
          ref_theo_(ref_theo),
          long_decay_(long_decay),
          short_decay_(short_decay),
          intensity_mult_(intensity_mult),
          vol_mult_(vol_mult) {
        auto base_md = g->add<RawMarketData>(base_theo->symbol());
        auto ref_md = g->add<RawMarketData>(ref_theo->symbol());
        auto rti = g->add<RefTradeIntensity>(base_md, ref_md, long_decay, short_decay);
        auto ct_clock = joinClocks(base_md, ref_md);
        auto ema_length = g->add<ScalarMult>(intensity_mult_, rti);
        base_ema_ = g->add<EMA>(base_theo, ct_clock, ema_length);
        ref_ema_ = g->add<EMA>(ref_theo, ct_clock, ema_length);

        setParents(base_theo, ref_theo, rti, ema_length, base_ema_, ref_ema_);
        setClock(ct_clock);
    }
};

struct PredictivePacketRate : public ValueNode {
    int64_t currentUptime() {
        return getGraph()->nSecUptime(); 
    }

    void decayEverything() {
        auto current_uptime_ = currentUptime();
        double nanos_elapsed = current_uptime_ - last_uptime_;
        double short_decay_factor = std::max(0.0, 1.0 - nanos_elapsed/ems_length_.count());
        double long_decay_factor = std::max(0.0, 1.0 - nanos_elapsed/long_ems_length_);

        base_long_ems_ *= long_decay_factor;
        ref_long_ems_ *= long_decay_factor;
        ref_short_ems_ *= short_decay_factor; 
        last_uptime_ = current_uptime_;
    }

    void compute() override {
        if ( unlikely(status_==StatusCode::INIT) ) {
            last_uptime_ = currentUptime();
        } else {
            decayEverything();
            if (ref_md_->ticked()) {
                ref_long_ems_ += 1 * ref_md_->tradeSize();
                ref_short_ems_ += 1 * ref_md_->tradeSize();
            }
            if (base_md_->ticked() and base_md_->tradeSize()>0) {
                base_long_ems_ += 1; 
            } 
            conditional_ema_.updateEMA(ref_short_ems_);
        }
        //value_ = conditional_ema_.value();
        value_ = ref_short_ems_;
        status_ = StatusCode::OK;
    }

    std::string defaultName() const override { 
        return (getClassName() + base_md_->shortSymbol() + ref_md_->shortSymbol() + getDurationString(ems_length_));
    }

    SERIALIZE(PredictivePacketRate, base_md_, ref_md_, ems_length_);

    MarketData* base_md_;
    MarketData* ref_md_;
    std::chrono::nanoseconds ems_length_;
    double base_long_ems_{0}, ref_long_ems_{0} , ref_short_ems_{0};
    SimpleEMA conditional_ema_;
    int64_t last_uptime_;
    double long_ems_length_;
    
    PredictivePacketRate(Graph* g, MarketData* base_md,
                       MarketData* ref_md, 
                       std::chrono::nanoseconds ems_length)
        : ValueNode(g, Units::NONE),
          base_md_(base_md),
          ref_md_(ref_md),
          ems_length_(ems_length) {
        conditional_ema_.setLength(5);
        long_ems_length_ = 1e9 * 60 * 30;
        setClock(g->add<OnTrade>(base_md), ref_md);
    }
};

struct PacketRateCompTheo : public Theo {
    void compute2() {
        value_ = 0;
        double max_val = 0;
        for ( auto p : components_ ) {
            if (max_val < p.first->heldValue()) {
                value_ = p.second->heldValue();
                max_val = p.first->heldValue(); 
            }
        }
        if (max_val==0) {
            value_ = base_theo_->heldValue();
        } else {
            value_ = std::min(value_, base_theo_->heldValue() + base_theo_->marketData()->tickSize());
            value_ = std::max(value_, base_theo_->heldValue() - base_theo_->marketData()->tickSize());
        }
        status_ = StatusCode::OK;
    }

     void compute() override {
        //Initialize this way to regularize when other weights are small.
        double sum = 1;
        value_ = base_theo_->heldValue();
        for ( auto p : components_ ) {
            double wgt = p.first->heldValue();
            sum += wgt;
            value_ += wgt * p.second->heldValue(); 
        }
        //Should we include the base theo in the weighting computation?  Then the sum will always be positive...
        if ( sum > 0) { //Test reverting to base_theo when weights have decayed a bit.
            value_ /= sum;
            value_ = std::min(value_, base_theo_->heldValue() + base_theo_->marketData()->tickSize());
            value_ = std::max(value_, base_theo_->heldValue() - base_theo_->marketData()->tickSize());
        } else {
            value_ = base_theo_->heldValue();
        }
        status_ = StatusCode::OK;
    }

    std::string defaultName() const override { 
        return (getClassName() + base_md_->shortSymbol() + getDurationString(ems_length_) + getDurationString(ct_length_));
    }

    SERIALIZE(PacketRateCompTheo, base_md_, ref_mds_, ems_length_, ct_length_);

    MarketData* base_md_;
    std::vector<MarketData*> ref_mds_;
    std::chrono::nanoseconds ems_length_, ct_length_;
    std::vector<std::pair<ValueNode*, TimeMaxCompTheo*>> components_;
    Theo* base_theo_;
    
    PacketRateCompTheo(Graph* g, MarketData* base_md, std::vector<MarketData*> ref_mds, std::chrono::nanoseconds ems_length, std::chrono::nanoseconds ct_length)
        : Theo(g, base_md->symbol()),
          base_md_(base_md),
          ref_mds_(ref_mds),
          ems_length_(ems_length),
          ct_length_(ct_length) {
        
        std::vector<ValueNode*> parents;
        base_theo_ = g->add<FillAve>(base_md, 2, 0.5, 100000,  4, false);
        for (auto ref_md : ref_mds ) {
            std::pair<ValueNode*, TimeMaxCompTheo*> p;
            Theo* ref_theo = g->add<FillAve>(ref_md, 2, 0.5, 100000,  4, false);
            double vol_mult = getVolMult(base_theo_->symbol(), ref_theo->symbol());
            p.first = g->add<PredictivePacketRate>(base_md, ref_md, ems_length_);
            p.second = g->add<TimeMaxCompTheo>(base_theo_, ref_theo, ct_length, vol_mult);
            components_.push_back(p);
            parents.push_back(p.first);
            parents.push_back(p.second);
        }
        setParents(parents);
        setClock(base_md, ref_mds);
    }
};


