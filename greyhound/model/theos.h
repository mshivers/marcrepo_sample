#pragma once
#include <cmath>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wparentheses"
#include <boost/circular_buffer.hpp>
#pragma GCC diagnostic pop

#include "model/graph.h"
#include "model/market_data.h"
#include "model/clocks.h"
#include "model/serialize.h"
#include "model/ema.h"

// Basic theoretical price signal implementations.

struct Midpt : public Theo {
    void compute() override {
        auto bid = market_data_->bidPrice();
        auto ask = market_data_->askPrice();

        value_ = (bid + ask) / 2.0;
        status_ = StatusCode::OK;
    }

    SERIALIZE(Midpt, market_data_);

    protected:
    Midpt(Graph* g, MarketData* market_data)
        : Theo(g, market_data) {
        setClock(g->add<OnBBOT>(market_data));
    }
};


struct WeightAve : public Theo {
    void compute() override {
        auto bid = market_data_->bidPrice();
        auto ask = market_data_->askPrice();
        auto bidSize = market_data_->bidSize();
        auto askSize = market_data_->askSize();

        double numer, denom;
        numer = (bid * askSize + ask * bidSize);
        denom = askSize + bidSize;
        value_ = numer / denom;
        status_ = StatusCode::OK;
    }

    SERIALIZE(WeightAve, market_data_);

    protected:
    WeightAve(Graph* g, MarketData* market_data)
        : Theo(g, market_data) {
        setClock(g->add<OnBBOT>(market_data));
    }
};

//templated on level_size to take both size and counts, which are different types
template<typename T>
double priceToFillImpl(MarketData* raw_market_data, 
                       const T* sizes_at_price, MarketData* market_data,
                       size_t max_depth, bool is_ask, double size_to_fill) {
    auto md_prices = is_ask ? market_data->askPricesPtr() : market_data->bidPricesPtr();
    auto inside_price = is_ask ? raw_market_data->askPricesPtr()[0].toDouble() : raw_market_data->bidPricesPtr()[0].toDouble();
    int direction = is_ask ? 1.0 : -1.0;
    double tick_size = market_data->tickSize();
    double stop_price = inside_price + direction * tick_size * (max_depth - 1);
    double price_to_execute{0};
    T left_to_trade = size_to_fill;

    for(size_t md_i=0; left_to_trade>0 && md_i<market_data->depth(); ++md_i) {
        if (md_prices[md_i].isZero())
            continue;

        double price_this_level = md_prices[md_i].toDouble();
        if(is_ask and price_this_level > stop_price + tick_size/2.)  
            break;
        if(not is_ask and price_this_level < stop_price - tick_size/2.)  
            break;
        
        T traded_this_level = std::min(sizes_at_price[md_i], left_to_trade);
        price_to_execute += traded_this_level * price_this_level;
        left_to_trade -= traded_this_level;
    }
    if ( left_to_trade > 0 ) {
        price_to_execute += stop_price * left_to_trade;
    }
    return price_to_execute;
};

struct PriceToFill : public Theo {
    void compute() override {
        const bool is_ask = side_ == Side::Ask;
        if ( use_counts_ ) {
            vpl::Int32 const* level_count;
            level_count = is_ask ? market_data_->askNumOrdersPtr() : market_data_->bidNumOrdersPtr();
            value_ = priceToFillImpl(raw_market_data_, level_count, market_data_,
                                     max_depth_, is_ask, size_->heldValue());
        } else {
            vpl::Int64 const* level_size;
            level_size = is_ask ? market_data_->askSizesPtr() : market_data_->bidSizesPtr();
            value_ = priceToFillImpl(raw_market_data_, level_size, market_data_,
                                     max_depth_, is_ask, size_->heldValue());
        }
        status_ = StatusCode::OK;
    }
    
    SERIALIZE(PriceToFill, market_data_, side_, size_, max_depth_, use_counts_);

    const ::Side side_;
    ValueNode* size_;  
    const size_t max_depth_;
    bool use_counts_;
    MarketData* raw_market_data_;
    protected:
    PriceToFill(Graph* g, MarketData* market_data, ::Side side,
                ValueNode* size, size_t max_depth, bool use_counts)
        : Theo(g, market_data),
          side_(side),
          size_(size),
          max_depth_(max_depth),
          use_counts_(use_counts),
          raw_market_data_(g->add<RawMarketData>(symbol())) {
        assert(max_depth>0);
        setParents(size_);
        setClock(market_data_, raw_market_data_);
    }
};

struct SizeFinder : public ValueNode {
    void compute() override {
        double size_found{0};
        if ( use_counts_ ) {
            size_found = book_depth_->bidCountToLevel(max_depth_) + 
                        book_depth_->askCountToLevel(max_depth_);
        } else {
            size_found = book_depth_->bidSizeToLevel(max_depth_) + 
                        book_depth_->askSizeToLevel(max_depth_);
        }
        size_found /= 2.0; //average for one side.

        simple_ema_.updateEMA(size_mult_ * size_found);
        value_ = std::ceil(std::max(1.0, simple_ema_.value()));  //round up to integer value
        status_ = StatusCode::OK;
    }
 
    std::string defaultName() const override { 
        return getClassName() + market_data_->shortSymbol() + (use_counts_?"Count":"Size");
    }

    SERIALIZE(SizeFinder, market_data_, max_depth_, size_mult_, ema_length_, use_counts_);

    const size_t max_depth_;
    double size_mult_;
    int ema_length_;
    bool use_counts_;
    BookDepth* book_depth_;
    MarketData* market_data_;
    SimpleEMA simple_ema_;
    
    protected:
    SizeFinder(Graph* g, MarketData* market_data, size_t max_depth, 
        double size_mult, int ema_length, bool use_counts)
        : ValueNode(g, Units::SIZE), 
          max_depth_(max_depth),  
          size_mult_(size_mult),  
          ema_length_(ema_length),
          use_counts_(use_counts),
          book_depth_(g->add<BookDepth>(market_data))
        , market_data_(market_data) {
        assert(size_mult>0);
        simple_ema_.setLength(ema_length);     
        setParent(book_depth_);
        setClock(book_depth_);
    }
};

//Use this instead of FillAve if you want to use a split book for the size-finder (FillAve always uses the full book)
struct AvgPriceExec : public ValueNode {
    void compute() override {
        auto totPrice = ask_fill_price_->heldValue() +
                        bid_fill_price_->heldValue(); 
        value_ = totPrice / (2 * size_->heldValue());
        status_ = StatusCode::OK;
    }

    SERIALIZE(AvgPriceExec, market_data_, size_, max_depth_, use_counts_);

    ValueNode* size_;
    size_t max_depth_;
    bool use_counts_;
    PriceToFill* bid_fill_price_;
    PriceToFill* ask_fill_price_;
    MarketData* market_data_;

    protected:
    AvgPriceExec(Graph* g, MarketData* market_data,
        ValueNode* size, size_t max_depth, bool use_counts)
        : ValueNode(g, Units::PRICE), 
          size_(size), 
          max_depth_(max_depth),
          use_counts_(use_counts),
          market_data_(market_data) {
        bid_fill_price_ = g->add<PriceToFill>(market_data, Side::Bid, size, 
                                              max_depth, use_counts);
        ask_fill_price_ = g->add<PriceToFill>(market_data, Side::Ask, size, 
                                              max_depth, use_counts);
        setParents(size_, bid_fill_price_, ask_fill_price_);
        setClock(bid_fill_price_, ask_fill_price_);
    }
};

struct FillAve : public Theo {
    void compute() override {
        auto a = ask_fill_price_->heldValue();
        auto b = bid_fill_price_->heldValue();
        
        value_ = (a + b) / (2 * size_->heldValue());
        status_ = StatusCode::OK;
    }

    SERIALIZE(FillAve, market_data_, size_depth_, size_mult_, size_ema_length_, 
            fill_depth_, use_counts_);

    const size_t size_depth_;
    double size_mult_;
    double size_ema_length_;
    const size_t fill_depth_;
    bool use_counts_;
    SizeFinder* size_;
    PriceToFill* bid_fill_price_;
    PriceToFill* ask_fill_price_;

    std::string defaultName() const override { 
        std::string md_desc = ( market_data_->isType<RawMarketData>() ? market_data_->shortSymbol() : market_data_->getName() );
        return getClassName() + (use_counts_?"Count":"Size") + md_desc;
    }
    
    FillAve(Graph* g, MarketData* market_data, size_t size_depth, double size_mult,
        double size_ema_length, size_t fill_depth, bool use_counts) 
        : Theo(g, market_data),
          size_depth_(size_depth), 
          size_mult_(size_mult), 
          size_ema_length_(size_ema_length),
          fill_depth_(fill_depth), 
          use_counts_(use_counts) {
        auto rmd = g->add<RawMarketData>(symbol());  //use RMD BBO for depth, so must clock on rmd
        //TODO(mshivers): test whether we should use rmd for SizeFinder, so sparse split books aren't poorly-behaved.
        size_ = g->add<SizeFinder>(rmd, size_depth_, size_mult_,
                                   size_ema_length_, use_counts_);
        bid_fill_price_ = g->add<PriceToFill>(market_data, Side::Bid, size_,
                                              fill_depth, use_counts);
        ask_fill_price_ = g->add<PriceToFill>(market_data, Side::Ask, size_,
                                              fill_depth, use_counts);
        setParents(bid_fill_price_, ask_fill_price_, size_);
        setClock(rmd);
    }
};

