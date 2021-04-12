#pragma once

#include "model/market_data.h"

namespace data_grab {

//TODO(mshivers): write a sampler that samples on every trade, and subsamples quotes based on how many packets 
//have arrived just before it (sample less the more previous packets). If last packet was trade, always sample the next update

// Sample on trades and 1 in N quotes
struct OneInN : public ClockNode
{
    void compute() override
    {
        ++n;
        ticked_ = market_data_->isTrade() || (n % oneInN_) == 0;
    }

    std::string defaultName() const override
    {
        return "OneInN" + std::to_string(oneInN_) + getShortSymbol(market_data_->symbol());
    }
    
    SERIALIZE(OneInN, market_data_, oneInN_);
    
    std::string symbol_;
    protected:
    MarketData* market_data_;
    int oneInN_;
    int n;
    
    OneInN(Graph* g, MarketData* market_data, int oneInN)
        : ClockNode(g)
        , market_data_(market_data)
        , oneInN_(oneInN)
        , n(0)
    {
        status_ = StatusCode::OK;
        setClock(g->add<OnUpdate>(market_data_));
    }
};

// Subsamples the clock of the input node
struct SubSample : public ClockNode
{
    void compute() override
    {
        ++n;
        ticked_ = (n % oneInN_) == 0;
        status_ = StatusCode::OK;
    }

    SERIALIZE(SubSample, parent_, oneInN_);
    
    Node* parent_;
    protected:
    int oneInN_;
    int n;
    
    SubSample(Graph* g, Node* parent, int oneInN)
        : ClockNode(g)
        , parent_(parent)
        , oneInN_(oneInN)
        , n(0)
    {
        setClock(parent);
    }
};

struct LockedBook : public ClockNode
{
    void compute() override
    {
        ticked_ = marketData_->bidPrice() >= marketData_->askPrice();
        status_ = StatusCode::OK;
    }

    SERIALIZE(LockedBook, marketData_);

    MarketData* marketData_;
    
    LockedBook(Graph* g, MarketData* marketData)
        : ClockNode(g), marketData_(marketData)
    {
        setClock(marketData);
    }
    
};

// Samples when a theo crosses a new 'grid line'
// Where grid lines equaly spaced
struct TheoChange : public ClockNode
{
    void compute() override;
    
    Theo* theo_;
    double changeInTicks_;
    double lowerBound_, upperBound_;
    double dTheo_;

    SERIALIZE(TheoChange, theo_, changeInTicks_);
    
    TheoChange(Graph* g, Theo* theo, double changeInTicks)
        : ClockNode(g), theo_(theo), changeInTicks_(changeInTicks)
        , lowerBound_(0), upperBound_(0)
        , dTheo_(changeInTicks * theo->market_data_->tickSize())
    {
        setClock(theo);
    }

};


// Samples when a theo crosses a new 'grid line'
// Where grid lines equaly spaced
struct TheoGridChange : public ClockNode
{
    void compute() override;
    
    Theo* theo_;
    int levelsPerTick_;
    double lowerBound_, upperBound_;
    double dLevel_;

    SERIALIZE(TheoGridChange, theo_, levelsPerTick_);
    
    TheoGridChange(Graph* g, Theo* theo, int levelsPerTick)
        : ClockNode(g), theo_(theo), levelsPerTick_(levelsPerTick)
        , lowerBound_(0), upperBound_(0)
        , dLevel_(theo->market_data_->tickSize() / levelsPerTick)
    {
        setClock(theo);
    }

};


} // namesapce data_grab
