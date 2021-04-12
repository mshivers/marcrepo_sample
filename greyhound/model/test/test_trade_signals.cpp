#include <chrono>

#include <gtest/gtest.h>

#include "model/test/clock_override.h"
#include "model/test/mock_bookmsg.h"
#include "model/test/mock_event_source_market_data.h"
#include "model/trade_signals.h"
#include "model/market_data.h"
#include "model/theos.h"

#include "model/test/utils.h"

using Units = ValueNode::Units;

struct test_trade_signals : public ::testing::Test, TestGraph
{
    test_trade_signals() : TestGraph("BTEC:US10Y", 1.)
    {
        btec = g->add<MockEventSourceMarketData>(symbol);
    }
    MockEventSourceMarketData* btec;
};

TEST(test_shrink_to_zero, shrink_to_zero) {
    ASSERT_EQ(shrinkToZero(1,2), 1);
    ASSERT_EQ(shrinkToZero(-1,2), 0);
    ASSERT_EQ(shrinkToZero(-2,-3), -2);
};

TEST_F(test_trade_signals, sigmoid_sv) {
    ASSERT_TRUE((btec));
    
    //2nd arg is half impact size; last arg is ema length in ticks
    auto sv_full_decay = g->add<SigmoidSV>(btec, 5, 1);
    auto sv_half_decay = g->add<SigmoidSV>(btec, 5, 2);
    ASSERT_TRUE((sv_full_decay));
    ASSERT_TRUE((sv_half_decay));

    md::Book bb;
    bb.insert(md::Order{1001, Side::Bid, 100, 99.0});
    bb.insert(md::Order{1002, Side::Ask, 200, 101.0});

    MockBookFiniteDepthMsg msg;
    msg.setOutrightBook(&bb);
    
    btec->fireBookChange(msg);
    
    ASSERT_TRUE(sv_full_decay->valid());
    ASSERT_TRUE(sv_half_decay->valid());
    //ASSERT_EQ(sv_full_decay->value(), 0);
    //ASSERT_EQ(sv_half_decay->value(), 0);

    //test a buy
    msg.addTrade(MockBookTradeMsg{5, 101});
    btec->fireBookChange(msg);
    
    ASSERT_EQ(sv_half_decay->value(), 0.5);
    ASSERT_EQ(sv_full_decay->value(), 0.5);
    
    msg.clearTrades();
    btec->fireBookChange(msg);

    ASSERT_LT(sv_half_decay->value(), 0.5);
    ASSERT_GT(sv_half_decay->value(), 0);
    ASSERT_EQ(sv_full_decay->value(), 0);

    msg.clearTrades();
    msg.addTrade(MockBookTradeMsg{5, 99}); //sell
    btec->fireBookChange(msg);
    ASSERT_EQ(sv_half_decay->value(), -0.5);
    msg.clearTrades();
    btec->fireBookChange(msg);
    ASSERT_GT(sv_half_decay->value(), -0.5);
}


TEST_F(test_trade_signals, ema_sigmoid_sv) {
    ASSERT_TRUE((btec));
    
    auto trade_size_ = g->add<SignedTradeSize>(btec);
    auto on_update = g->add<OnUpdate>(btec);
    auto padded_trade_size = g->add<Pad>(trade_size_, on_update, 0);
    auto trade_size_ema = g->add<TickEMA>(padded_trade_size, on_update, 2);
    ASSERT_TRUE((trade_size_ema));
    //2nd arg is half impact size; last arg is ema length in ticks
    auto sv_full_decay = g->add<EMSSigmoidSV>(btec, 5, 1);
    auto sv_half_decay = g->add<EMSSigmoidSV>(btec, 5, 2);

    ASSERT_TRUE((sv_full_decay));
    ASSERT_TRUE((sv_half_decay));

    md::Book bb;
    bb.insert(md::Order{1001, Side::Bid, 100, 99.0});
    bb.insert(md::Order{1002, Side::Ask, 200, 101.0});

    MockBookFiniteDepthMsg msg;
    msg.setOutrightBook(&bb);
    
    btec->fireBookChange(msg);
    
    ASSERT_TRUE(sv_full_decay->valid());
    ASSERT_TRUE(sv_half_decay->valid());
    //ASSERT_EQ(sv_full_decay->value(), 0);
    //ASSERT_EQ(sv_half_decay->value(), 0);

    //test a buy
    msg.addTrade(MockBookTradeMsg{5, 101});
    btec->fireBookChange(msg);
    
    ASSERT_EQ(sv_half_decay->value(), 0.5);
    ASSERT_EQ(sv_full_decay->value(), 0.5);
    
    msg.clearTrades();
    btec->fireBookChange(msg);

    ASSERT_LT(sv_half_decay->value(), 0.5);
    ASSERT_GT(sv_half_decay->value(), 0);
    ASSERT_EQ(sv_full_decay->value(), 0);

    msg.clearTrades();
    msg.addTrade(MockBookTradeMsg{5, 99}); //sell
    btec->fireBookChange(msg);
    ASSERT_LT(sv_half_decay->value(), 0);
    ASSERT_GT(sv_half_decay->value(), -0.5);
    ASSERT_EQ(sv_full_decay->value(), -0.5);
}

TEST_F(test_trade_signals, PersistentSV) {
    ASSERT_TRUE((btec));
    
    //args are half impact size/ema tick length, for fast/slow resp.
    auto tp = g->add<PersistentSV>(btec, 5.0, 2, 20.0, 1000);
    ASSERT_TRUE((tp));

    md::Book b;
    MockBookFiniteDepthMsg msg;
    msg.setOutrightBook(&b);
    
    b.insert(md::Order{1001, Side::Bid, 50, 99.0});
    b.insert(md::Order{1002, Side::Ask, 30, 101.0});
    btec->fireBookChange(msg);

    // no trades yet: svs fire on quotes with value = 0 only after the first trade, but is valid when constructed
    ASSERT_TRUE(tp->valid());

    //test a buy: fast sum = slow sum = 20
    msg.addTrade(MockBookTradeMsg{20, 101});
    btec->fireBookChange(msg);
    msg.clearTrades();
    
    // slow sv = 0.5 < fast sv.
    ASSERT_EQ(tp->value(), 0.5);

    // fast sum = 20*0.5 = 10; slow sum = 20 * .999 = 19.98
    btec->fireBookChange(msg);  //decay once on quote
    
    // opposite signed trade: fast sum=10*0.5-15=-10; slow sum=19.98*0.999-15=4.96
    msg.addTrade(MockBookTradeMsg{15, 99});
    btec->fireBookChange(msg);
    msg.clearTrades();
    ASSERT_EQ(tp->value(), 0);
 
    //decay updates
    btec->fireBookChange(msg);
    btec->fireBookChange(msg);

    // large same signed so both SVs>0 and minSV<0.5 (slow one_
    msg.addTrade(MockBookTradeMsg{15, 101});
    btec->fireBookChange(msg);
    msg.clearTrades();
    ASSERT_GT(tp->value(), 0);

    //no trades, just decay
    for (int i=0; i<10; ++i) {
        auto last_sv = tp->heldValue();
        btec->fireBookChange(msg);
        ASSERT_LT(tp->value(), 0.5);
        ASSERT_LT(tp->value(), last_sv);
        ASSERT_GT(tp->value(), 0);
    }
}

TEST_F(test_trade_signals, ProdSV) {
    std::string symbol5{"BTEC:US5Y"};
    std::string symbol10{"BTEC:US10Y"};

    // DummyRefData is needed to set a huge tick size
    // So the the book that are set are "safeUpdate"
    DummyRefData refData(100, 100, 100, 100, 10);

    Universe universe;
    universe.add(symbol5, &refData);
    universe.add(symbol10, &refData);
    Strategy strategy;
    strategy.setUniverse(universe);
    Graph* g = strategy.newGraph();


    auto btec5 = g->add<MockEventSourceMarketData>(symbol5);
    auto btec10 = g->add<MockEventSourceMarketData>(symbol10);
    ASSERT_TRUE((btec5));
    ASSERT_TRUE((btec10));
    
    //2nd arg is half impact size; last arg is ema length in ticks
    auto prodsv = g->add<ProdSV>(btec5, 10, btec10, 5, 2);
    ASSERT_TRUE((prodsv));

    md::Book b5, b10;
    MockBookFiniteDepthMsg msg5, msg10;
    msg5.setOutrightBook(&b5);
    msg10.setOutrightBook(&b10);

    b5.insert(md::Order{1001, Side::Bid, 50, 99.0});
    b5.insert(md::Order{1002, Side::Ask, 30, 101.0});
    btec5->fireBookChange(msg5);

    b10.insert(md::Order{1001, Side::Bid, 10, 110.0});
    b10.insert(md::Order{1002, Side::Ask, 20, 111.0});
    btec10->fireBookChange(msg10);
    
    ASSERT_TRUE(prodsv->valid());

    //test a 5y buy
    msg5.addTrade(MockBookTradeMsg{20, 101});
    btec5->fireBookChange(msg5);
    msg5.clearTrades();
    
    ASSERT_TRUE(prodsv->valid());
    
    //5y ema decays to 10 == half_impact_size
    btec5->fireBookChange(msg5);

    //10y trade is half_impact_size, so prodsv=0.5
    msg10.addTrade(MockBookTradeMsg{5, 111});
    btec10->fireBookChange(msg10);
    msg10.clearTrades();
    ASSERT_EQ(prodsv->value(), 0.5);

    //no trades, just decay
    for (int i=0; i<10; ++i) {
        auto last_sv = prodsv->heldValue();
        btec5->fireBookChange(msg5);
        btec10->fireBookChange(msg10);
        ASSERT_LT(prodsv->value(), 0.5);
        ASSERT_LT(prodsv->value(), last_sv);
        ASSERT_GT(prodsv->value(), 0);
    }

    msg10.addTrade(MockBookTradeMsg{5, 110}); //sell
    btec10->fireBookChange(msg10);

    //opposite sign svs, so prodsv=0
    ASSERT_EQ(prodsv->value(), 0);
}

#ifdef REPLAY_BUILD
TEST_F(test_trade_signals, trade_intensity) {
    ASSERT_TRUE((btec));
    
    using millis = std::chrono::milliseconds;
    using nanos = std::chrono::nanoseconds;
    nanos long_decay = millis{60000000}; 
    nanos short_decay = millis{100};

    //2nd arg is min size for impulse
    auto ti = g->add<TradeIntensity>(btec, long_decay, short_decay);
    ASSERT_TRUE((ti));

    md::Book b;
    MockBookFiniteDepthMsg msg;
    msg.setOutrightBook(&b);

    b.insert(md::Order{1001, Side::Bid, 50, 99.0});
    b.insert(md::Order{1002, Side::Ask, 30, 101.0});
    btec->fireBookChange(msg);

    // no trades yet 
    ASSERT_EQ(ti->ticked(), false);
    ASSERT_EQ(ti->valid(), false);
    //ASSERT_EQ(ti->heldValue(), 0);

    //test a buy
    msg.addTrade(MockBookTradeMsg{10, 101});
    btec->fireBookChange(msg);
    msg.clearTrades();
    ASSERT_EQ(ti->value(), 1);

    clock_override clock;
    Int64 start;
    double length, decay, elapsed, answer;
    
    start = g->nSecUptime();
    clock.incrementTime(millis{50}); // halflife of short sum

    msg.addTrade(MockBookTradeMsg{4, 101});
    btec->fireBookChange(msg);
    msg.clearTrades();

    elapsed = g->nSecUptime() - start;
    length = short_decay.count() / elapsed;
    decay = (length - 1) / length;
    if ( decay < 0 ) decay = 0;
    answer = (10 * decay + 4);  //~~ 9
 
    //short sum = 10/2+4=9; long sum ~= 14; ti = 9/14
    ASSERT_NEAR(ti->long_sum_->value(), 14, 0.2);
    ASSERT_NEAR(ti->value(), answer/ti->long_sum_->value(), 0.0001);

    start = g->nSecUptime();
    clock.incrementTime(millis{50}); // halflife of short sum

    //test a small sell 
    msg.addTrade(MockBookTradeMsg{2, 99});
    btec->fireBookChange(msg);
    msg.clearTrades();
    //short sum = 9/2+2=6.5; long sum ~=16
    
    elapsed = g->nSecUptime() - start;
    length = short_decay.count() / elapsed;
    decay = (length - 1) / length;
    if ( decay < 0 ) decay = 0;
    answer = (answer * decay + 2);  //~~6.5 
    ASSERT_NEAR(ti->long_sum_->value(), 16, 0.2);
    ASSERT_NEAR(ti->value(), answer/ti->long_sum_->value(), 0.0001);
    
    //decay short sum fully
    clock.incrementTime(millis{200}); //2 x short length
    //
    //test a small sell 
    msg.addTrade(MockBookTradeMsg{1, 99});
    btec->fireBookChange(msg);
    msg.clearTrades();
    ASSERT_NEAR(ti->value(), 1.0/17.0, 0.1);

    // test no trade
    btec->fireBookChange(msg);
    ASSERT_EQ(ti->ticked(), false);
}
#endif


