
#pragma once
#include <cmath>

#include "model/clocks.h"
#include "model/graph.h"
#include "model/market_data.h"
#include "model/private_msg.h"
#include "model/serialize.h"
#include "model/theos.h"
#include "model/traded_symbol.h"

//Basic protection adjuster (PA) nodes that can veto outgoing messaging.

//TODO(mshivers): test using a clock ema for the depth_ema in LowLiquidity.  A tick ema is not likely to work well after an event.

//Clock to flag when the traded market has become dangerously thin
struct LowLiquidity : public ValueNode {
    void compute() override {
        double current_depth{0};
        if ( use_counts_ ) {
            current_depth = book_depth_->bidCountToLevel(max_depth_) + 
                        book_depth_->askCountToLevel(max_depth_);
        } else {
            current_depth = book_depth_->bidSizeToLevel(max_depth_) + 
                        book_depth_->askSizeToLevel(max_depth_);
        }

        if ( unlikely(status_==StatusCode::INIT) )
            depth_ema_ = current_depth;
        else
            depth_ema_ += (current_depth - depth_ema_) / ema_tick_length_;

        value_ = current_depth < trigger_fraction_ * depth_ema_;
        status_ = StatusCode::OK;
    }
 
    SERIALIZE(LowLiquidity, symbol_, max_depth_, use_counts_, trigger_fraction_, ema_tick_length_);

    RawMarketData* market_data_;
    BookDepth* book_depth_;
    double depth_ema_;

    std::string symbol_;
    size_t max_depth_;
    bool use_counts_;
    double trigger_fraction_;
    double ema_tick_length_;

    protected:
    LowLiquidity(Graph* g, std::string const& symbol, size_t max_depth, bool use_counts, 
            double trigger_fraction, double ema_tick_length)
        : ValueNode(g), 
          symbol_(symbol), 
          max_depth_(max_depth),
          use_counts_(use_counts),
          trigger_fraction_(trigger_fraction),
          ema_tick_length_(ema_tick_length) {
        assert(trigger_fraction < 1);
        book_depth_ = g->add<BookDepth>(g->add<RawMarketData>(symbol_));
        setParent(book_depth_);
        setClock(book_depth_);
    }
};

//TODO(mshivers): does FastMarket midpt tick change work when sweep trades come in multiple packets, or will there be a
//sequence of half tick changes? We could adjust this to only check on updates where the exchangeTime has changed, so if
//there are multiple orderUpdates that incrementally update the book, the cummulative change will go into the FM
//adjuster.

//FastMarket catches the case where there's a multi-symbol sweep but we haven't gotten the other legs of the sweep.  
//So wait something like 10 milliseconds after a 1 ticksize change in the traded midpt before sending resting orders
//in the opposite direction.
struct FastMarket : public ValueNode {
    void compute() override {
        if ( unlikely(status_==StatusCode::INIT) )
            value_ = false;
        else {
            double theo_change = midpt_->heldValue() - lag_;
            if ( (no_order_side_ == Side::Ask and theo_change > ticksize_) or 
                 (no_order_side_ == Side::Bid and theo_change < -ticksize_) )
                last_trigger_time_ = graph_->nSecUptime();

            value_ = graph_->nSecUptime() - last_trigger_time_ < wait_nanos_;
        }

        lag_ = midpt_->heldValue();
        status_ = StatusCode::OK;
    }
 
    SERIALIZE(FastMarket, symbol_, no_order_side_, wait_duration_);

    std::string symbol_;
    Side no_order_side_;  
    std::chrono::nanoseconds wait_duration_;
    double ticksize_;
    RawMarketData* market_data_;
    Midpt* midpt_;
    double lag_;
    vpl::Int64 last_trigger_time_{0};
    vpl::Int64 wait_nanos_;

    protected:
    FastMarket(Graph* g, std::string const& symbol, Side no_order_side, 
               std::chrono::nanoseconds wait_duration)
        : ValueNode(g), 
          symbol_(symbol),
          no_order_side_(no_order_side),
          wait_duration_(wait_duration) {
        auto strategy = g->getStrategy();
        assert(strategy);
        auto traded_symbol = strategy->findSymbol(symbol);
        assert(traded_symbol); 
        ticksize_ = traded_symbol->tickSize();
        
        market_data_ = g->add<RawMarketData>(symbol);
        midpt_ = g->add<Midpt>(market_data_);
        wait_nanos_ = wait_duration.count();
        setParent(midpt_);
        setClock(g->add<OnUpdate>(market_data_));
    }
};

//Prevent sending orders when the market is too wide.  Typically we'll not send orders if the market is 3-ticks wide or
//more. This also prevents adding models from repeatedly narrowing a large spread.
//TODO(mshivers): update WideSpread so that is uses SpreadWA instead of b/a spread.  Often right after a number
//the b/a spread is 1-tick, but the inside size is tiny, so it's effectively 3 ticks.  There's a SpreadAve state node
struct WideSpread : public ValueNode {
    void compute() override {
        double spread = market_data_->askPrice() - market_data_->bidPrice();
        value_ = spread >= market_data_->tickSize() * wide_ticks_;
        status_ = StatusCode::OK;
    }
 
    SERIALIZE(WideSpread, symbol_, wide_ticks_);

    std::string symbol_;
    RawMarketData* market_data_;
    int wide_ticks_;

    protected:
    WideSpread(Graph* g, std::string const& symbol, int wide_ticks)
        : ValueNode(g), 
          symbol_(symbol),
          wide_ticks_(wide_ticks) {
        market_data_ = g->add<RawMarketData>(symbol);
        setClock(g->add<OnBBOT>(market_data_));
    }
};

//prevent sending orders when valuation is too far through the book of the valuation symbol
struct ThruBook : public ValueNode {
    void compute() override {
        double max_outside = tick_size_ * ticks_too_far;
        double max_val = market_data_->askPrice() + max_outside;
        double min_val = market_data_->bidPrice() - max_outside;
        value_ = valuation_->value() < min_val or valuation_->value() > max_val;
        status_ = StatusCode::OK;
    }
 
    SERIALIZE(ThruBook, valuation_, ticks_too_far);

    protected:
    
    Theo* valuation_;
    int ticks_too_far;
    double tick_size_;
    RawMarketData* market_data_;
    
    ThruBook(Graph* g, Theo* valuation, int ticks_too_far)
        : ValueNode(g), 
          valuation_(valuation),
          ticks_too_far(ticks_too_far),
          tick_size_(g->getStrategy()->findSymbol(valuation->symbol())->tickSize())
    {
        market_data_ = g->add<RawMarketData>(valuation->symbol());
        setParents(market_data_, valuation);
        setClock(valuation);
    }
};

//TODO(mshivers): When we begin trading DWEB, modify TimeThruBook so it takes a reference market.  
//We want to assure that the DWEB valuation is not thru the BTEC book. 
//prevent orders if valuation remains through the book for too long.
struct TimeThruBook : public ValueNode {
    void compute() override {
        double max_outside = market_data_->tickSize() * ticks_too_far;
        double max_val = market_data_->askPrice() + max_outside;
        double min_val = market_data_->bidPrice() - max_outside;
        if ( valuation_->value() < min_val or valuation_->value() > max_val ) {
            if ( not currently_thru_book_ ) {
                start_thru_book_time_ = graph_->nSecUptime();
                currently_thru_book_ = true;
            } 
            value_ = graph_->nSecUptime() - start_thru_book_time_ > min_duration_.count();
        } else {
            currently_thru_book_ = false;
            value_ = false;
        }
        status_ = StatusCode::OK;
    }
 
    SERIALIZE(TimeThruBook, valuation_, ticks_too_far, min_duration_);

    Theo* valuation_;
    int ticks_too_far;
    std::chrono::nanoseconds min_duration_;
    RawMarketData* market_data_;
    vpl::Int64 start_thru_book_time_;
    bool currently_thru_book_{false};

    protected:
    TimeThruBook(Graph* g, Theo* valuation, int ticks_too_far, 
            std::chrono::nanoseconds min_duration)
        : ValueNode(g), 
          valuation_(valuation),
          ticks_too_far(ticks_too_far),
          min_duration_(min_duration) {
        market_data_ = g->add<RawMarketData>(valuation->symbol());
        setParents(market_data_, valuation);
        setClock(valuation);
    }
};

//Use this as and input for a sanctioner.
struct SafeUpdateFailed : public ValueNode {
    void compute() override {
        status_ = StatusCode::OK;
        value_ = false;
        for (auto rmd_ : rmds_ ) {
            if (not rmd_->safeUpdate()) {
                value_ = true;
                failed_symbol_ = rmd_->symbol();
                LOG_INFO() << "FAILED safeUpdate: " << failed_symbol_ << " bid: " << rmd_->bidPrice() << " ask: " << rmd_->askPrice() << " ticksize: " << rmd_->tickSize()
                           << " bidSize: " << rmd_->bidSize() << " askSize: " <<   rmd_->askSize() 
                           << " bidNumOrders: " << rmd_->bidNumOrders() << " askNumOrders: " <<   rmd_->askNumOrders();
                return;
            }
        }
    }
 
    std::string failedSymbol() {
        if (value_ == true)
            return failed_symbol_;
        return "";
    }

    SERIALIZE(SafeUpdateFailed, valuation_);

    Theo* valuation_;
    RawMarketData* market_data_;
    std::set<RawMarketData*> rmds_;
    std::string failed_symbol_;

    protected:
    SafeUpdateFailed(Graph* g, Theo* valuation) 
        : ValueNode(g), 
          valuation_(valuation) {
        value_ = true;
        market_data_ = graph_->add<RawMarketData>(valuation->symbol());
        rmds_ = graph_->getNodes<RawMarketData>();
        setClock(market_data_);
    }
};


//BadMarkups prevents a model from trading if too many recent fills lost money.  
//Rule that seems to work best is accumDecay 30s markup sign, decayed at 90%, and turn model off for the rest of the day when > 5-6
struct BadMarkups : public ValueNode {
    using TimeSidePrice = std::tuple<int64_t, int, double>;

    void updateMarkupCount(TimeSidePrice old_fill) {
        bad_markup_ems_ *= decay_pct_;
        double current_mid = midpt_->heldValue();
        auto trade_dir = std::get<1>(old_fill);
        auto price = std::get<2>(old_fill);
        if ( price != current_mid ) {
            if ( trade_dir > 0 ) {
                if ( current_mid < price )
                    bad_markup_ems_ += 1;
                else 
                    bad_markup_ems_ -= 1;
            } else {
                if ( current_mid < price )
                    bad_markup_ems_ -= 1;
                else 
                    bad_markup_ems_ += 1;
            }
        }
    }

    void append(TimeSidePrice value) {
        if ( stored_values_.full() ) {
            auto old_fill = stored_values_.front();
            updateMarkupCount(old_fill);
            stored_values_.pop_front();
        }
        stored_values_.push_back(value);
    }

    void compute() override {
        if ( private_msg_->ticked() ) {
            const OrderUpdate& order_update = private_msg_->orderUpdate();
            if(order_update.updateType == OrderUpdate::UpdateType::Fill)
            {
                auto price = order_update.price;
                auto side = order_update.side;
                if ( (price > 0) && (side != 0) )
                {
                    int64_t new_markup_time = graph_->nSecUptime() + markup_horizon_.count();
                    int trade_dir = ( side > 0 ) ? 1 : -1;
                    TimeSidePrice new_fill(new_markup_time, trade_dir, price);
                    append(new_fill);    
                }
            }
        }
        
        int64_t current_time = graph_->nSecUptime();
        while ( not stored_values_.empty() ) {
            int64_t markup_time = std::get<0>(*stored_values_.begin());
            if ( current_time >= markup_time ) {
                auto old_fill = stored_values_.front();
                updateMarkupCount(old_fill);
                stored_values_.pop_front();
            } else {
                break;
            }
        }

        //set to true first time this exceeds the threshold, and never change back
        if ( bad_markup_ems_ > threshold_ )
            value_ = true;

        status_ = StatusCode::OK;
    }

    SERIALIZE(BadMarkups, order_logic_name_, markup_horizon_, decay_pct_, threshold_, buffer_size_);
    
    double bad_markup_ems_{0};

    protected:
    std::string order_logic_name_;
    std::chrono::nanoseconds markup_horizon_;
    double decay_pct_;
    double threshold_;
    int buffer_size_;
    MsgAck* private_msg_;
    boost::circular_buffer<TimeSidePrice> stored_values_;
    ValueNode* midpt_;

    BadMarkups(Graph* g, std::string order_logic_name, std::chrono::nanoseconds markup_horizon, 
               double decay_pct, double threshold, int buffer_size);
    
};

struct BadMarkupCount : public ValueNode {
    void compute() override {
        value_ = bad_markups_->bad_markup_ems_; 
        status_ = StatusCode::OK;
    }

    SERIALIZE(BadMarkupCount, order_logic_name_, markup_horizon_, decay_pct_, threshold_, buffer_size_);
    
    protected:
    std::string order_logic_name_;
    std::chrono::nanoseconds markup_horizon_;
    double decay_pct_;
    double threshold_;
    int buffer_size_;
    BadMarkups* bad_markups_;

    BadMarkupCount(Graph* g, std::string order_logic_name, std::chrono::nanoseconds markup_horizon, 
               double decay_pct, double threshold, int buffer_size);
    
};

//RecentFill prevents sending a new order within some time after a fill on the same side
struct RecentFill : public ValueNode {
    void compute() override {
        int64_t current_time = graph_->nSecUptime();

        if ( private_msg_->ticked() ) {
            const OrderUpdate& order_update = private_msg_->orderUpdate();
            if(order_update.updateType == OrderUpdate::UpdateType::Fill) {
                auto price = order_update.price;
                if (price > 0) {
                    auto side = order_update.side;
                    bool matched_side = ( (no_order_side_ == Side::Ask and side < 0) or 
                                          (no_order_side_ == Side::Bid and side > 0) );
                    if ( matched_side ) 
                        earliest_order_time_ = current_time + wait_duration_.count();
                }
            }
        }
        
        if ( current_time <= earliest_order_time_ )
            value_ = true;
        else
            value_ = false;

        status_ = StatusCode::OK;
    }

    SERIALIZE(RecentFill, order_logic_name_, no_order_side_, wait_duration_);
    
    protected:
    std::string order_logic_name_;
    Side no_order_side_;
    std::chrono::nanoseconds wait_duration_;
    int64_t earliest_order_time_{0};
    MsgAck* private_msg_;

    RecentFill(Graph* g, std::string order_logic_name, Side no_order_side, 
               std::chrono::nanoseconds wait_duration);
    
};

//Valuation must return to inside the book before additional IOCs are allowed
struct IOCAlreadySent: public ValueNode {
 
    bool valuationThruSide(int8_t side) {
        if ( side > 0 )
            return valuation_->heldValue() > md_->askPrice();
        if ( side < 0 )
            return valuation_->heldValue() < md_->bidPrice();
        return false;
    }
     
    bool valuationThruBook() {
        return valuationThruSide(1) || valuationThruSide(-1);
    }

    bool isIOC(const NewOrderRequest& new_order) {
        if ( new_order.side > 0 && new_order.price >= md_->askPrice() )
            return true;
        if ( new_order.side < 0 && new_order.price <= md_->bidPrice() )
            return true;
        return false;
    }

    void compute() override {
        if ( send_msg_->ticked() ) {
            for(const OrderRequest& order_request: send_msg_->orderRequest()) {
                if( order_request.is<NewOrderRequest>() ) {
                    auto new_order = order_request.get<NewOrderRequest>();
                    if( isIOC(new_order) && valuationThruSide(new_order.side) )
                        value_ = true;
                }
            }
        }
        if ( value_ && not valuationThruBook() ) {
            value_ = false;
        } 
        
        status_ = StatusCode::OK;
    }

    SERIALIZE(IOCAlreadySent, order_logic_name_);
    
    protected:
    std::string order_logic_name_;
    Theo* valuation_;
    RawMarketData* md_;
    SendMsg* send_msg_;

    IOCAlreadySent(Graph* g, std::string order_logic_name);
};




