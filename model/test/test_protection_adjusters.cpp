#include <chrono>

#include <gtest/gtest.h>

#include "model/test/clock_override.h"
#include "model/test/mock_bookmsg.h"
#include "model/test/mock_node.h"

#include "model/market_data.h"
#include "model/protection_adjusters.h"
#include "model/theos.h"
#include "model/trade_signals.h"
#include "model/test/clock_override.h"
#include "model/test/utils.h"
#include "model/test/mock_event_source_market_data.h"

#include <vpl/Price.hpp>


using Units = ValueNode::Units;
using testing::ReturnRef;
using testing::NiceMock;

struct test_protection_adjusters : public ::testing::Test, TestGraphMultiSym
{

    test_protection_adjusters() : TestGraphMultiSym({"BTEC:US10Y", "BTEC:US5Y"}, {1., 1.})
    {}
};


TEST_F(test_protection_adjusters, low_liquidity) {
    std::string symbol{"BTEC:US10Y"};
    auto btec = g->add<MockEventSourceMarketData>(symbol);
    ASSERT_TRUE((btec));
    
    auto bd = g->add<BookDepth>(btec);
    ASSERT_TRUE((bd));
    auto ll = g->add<LowLiquidity>(symbol, 
                                  3, //max_depth
                                  false, //use_counts
                                  0.5, //trigger fraction
                                  1000); //ema_tick_length
    ASSERT_TRUE((ll));

    md::Book bb;
    NiceMock<MockBookFiniteDepthMsg> msg;
    msg.setOutrightBook(&bb);
    
    bb.insert(md::Order{1001, Side::Bid, 100, 99.0});
    bb.insert(md::Order{1002, Side::Bid, 200, 98.0});
    bb.insert(md::Order{1003, Side::Bid, 7000, 97.0});
    bb.insert(md::Order{1004, Side::Bid, 90000, 96.0});

    bb.insert(md::Order{2001, Side::Ask, 100, 100.0});
    bb.insert(md::Order{2002, Side::Ask, 200, 101.0});
    bb.insert(md::Order{2003, Side::Ask, 8000, 102.0});
    bb.insert(md::Order{2004, Side::Ask, 90000, 103.0});

    btec->fireBookChange(msg);  // set initial depth ema
    ASSERT_TRUE(bd->ticked());
    ASSERT_TRUE(ll->parentsValid());
    ASSERT_FALSE(ll->heldValue());
   
    //cancel orders too deep to matter
    bb.cancel(1004);
    bb.cancel(2004);

    btec->fireBookChange(msg);  
    ASSERT_TRUE(bd->ticked());
    ASSERT_FALSE(ll->heldValue());
    
    //cancel orders to trigger firing 
    bb.cancel(1003);
    bb.cancel(2003);

    //decay for a while; should remain ticking
    for (int i=0; i<100; ++i) {
        btec->fireBookChange(msg);  
        ASSERT_TRUE(bd->ticked());
        ASSERT_TRUE(ll->heldValue());
    }
    
    //add liquidity to stop clock
    bb.insert(md::Order{1005, Side::Bid, 8000, 97.0});
    bb.insert(md::Order{2005, Side::Ask, 9000, 102.0});

    btec->fireBookChange(msg);  
    ASSERT_TRUE(bd->ticked());
    ASSERT_FALSE(ll->heldValue());

}

#ifdef REPLAY_BUILD
TEST_F(test_protection_adjusters, fast_market) {
    std::string symbol{"BTEC:US5Y"};
    auto btec = g->add<MockEventSourceMarketData>(symbol);
    ASSERT_TRUE((btec));
    
    using millis = std::chrono::milliseconds;
    auto one_sec = millis{1000}; 
    auto fm = g->add<FastMarket>(symbol, Side::Ask, one_sec);
    ASSERT_TRUE((fm));
    ASSERT_FALSE((fm->valid()));

    clock_override clock;
    clock.incrementTime(millis{1000});

    md::Book b;
    NiceMock<MockBookFiniteDepthMsg> msg;
    msg.setOutrightBook(&b);
    
    ASSERT_FALSE(fm->valid());
    // initialize fm lagged theo
    b.insert(md::Order{1001, Side::Bid, 5, 100.0});
    b.insert(md::Order{2001, Side::Ask, 50, 101.0});
    btec->fireBookChange(msg);
    ASSERT_TRUE(fm->valid());
    ASSERT_FALSE(fm->heldValue());

    b.insert(md::Order{1002, Side::Bid, 1, 100.0});
    btec->fireBookChange(msg);
    ASSERT_FALSE(fm->heldValue());

    //change weightave by more than a tick
    b.cancel(2001);
    b.insert(md::Order{2002, Side::Ask, 1, 104.0});
    btec->fireBookChange(msg);
    ASSERT_TRUE(fm->heldValue());

    //revert to old theo
    b.insert(md::Order{2003, Side::Ask, 50, 101.0});
    btec->fireBookChange(msg);
    ASSERT_TRUE(fm->heldValue());
    b.insert(md::Order{2004, Side::Ask, 1, 101.0});
    btec->fireBookChange(msg);
    ASSERT_TRUE(fm->heldValue());

    //should remain ticking for 1 second with no theo change
    //not enough time to end fast market tick
    clock.incrementTime(millis{500}); 
    btec->fireBookChange(msg);
    ASSERT_TRUE(fm->heldValue());

    //now it should be over.
    clock.incrementTime(millis{600}); 
    btec->fireBookChange(msg);
    ASSERT_FALSE(fm->heldValue());
} 
#endif

TEST_F(test_protection_adjusters, wide_spread) {
    std::string symbol{"BTEC:US5Y"};
    auto btec = g->add<MockEventSourceMarketData>(symbol);
    ASSERT_TRUE((btec));
    
    auto ws = g->add<WideSpread>(symbol, 3);
    ASSERT_TRUE((ws));
    ASSERT_FALSE((ws->valid()));

    md::Book b;
    NiceMock<MockBookFiniteDepthMsg> msg;
    msg.setOutrightBook(&b);

    ASSERT_FALSE(ws->valid());

    //1 tick wide
    b.insert(md::Order{1001, Side::Bid, 5, 100.0});
    b.insert(md::Order{2001, Side::Ask, 50, 101.0});
    btec->fireBookChange(msg);
    ASSERT_TRUE(ws->valid());
    ASSERT_FALSE(ws->heldValue());

    //2 ticks wide
    b.insert(md::Order{1002, Side::Bid, 1, 99.0});
    b.cancel(1001);
    btec->fireBookChange(msg);
    ASSERT_TRUE(ws->valid());
    ASSERT_FALSE(ws->heldValue());
    
    //3 ticks wide
    b.insert(md::Order{1003, Side::Bid, 1, 98.0});
    b.cancel(1002);
    btec->fireBookChange(msg);
    ASSERT_TRUE(ws->valid());
    ASSERT_TRUE(ws->heldValue());
    
    //4 ticks wide
    b.cancel(1003);
    b.insert(md::Order{1004, Side::Bid, 1, 97.0});
    btec->fireBookChange(msg);
    ASSERT_TRUE(ws->valid());
    ASSERT_TRUE(ws->heldValue());

    //back to 2 ticks
    b.insert(md::Order{2002, Side::Ask, 5, 98.0});
    btec->fireBookChange(msg);
    ASSERT_TRUE(ws->valid());
    ASSERT_FALSE(ws->heldValue());
} 

TEST_F(test_protection_adjusters, thru_book) {
    std::string symbol{"BTEC:US5Y"};
    auto btec = g->add<MockEventSourceMarketData>(symbol);
    ASSERT_TRUE((btec));

    MockTheo valuation(g, symbol, btec);
 
    auto tb = g->add<ThruBook>(&valuation, 1);
    ASSERT_TRUE((tb));
    ASSERT_FALSE((tb->valid()));

    md::Book b;
    NiceMock<MockBookFiniteDepthMsg> msg;
    msg.setOutrightBook(&b);
    btec->fireBookChange(msg);

    ASSERT_FALSE(tb->valid());

    b.insert(md::Order{1001, Side::Bid, 1000, 100.0});
    b.insert(md::Order{2001, Side::Ask, 1000, 101.0});

    valuation.setValue(100.5);
    btec->fireBookChange(msg);
    ASSERT_TRUE(tb->valid());
    ASSERT_FALSE(tb->heldValue());

    //1.49 ticks thru ask
    valuation.setValue(101 + 1.49);
    btec->fireBookChange(msg);
    ASSERT_TRUE(tb->valid());
    ASSERT_TRUE(tb->heldValue());
    
    //.49 ticks outside ask
    valuation.setValue(101 + 0.49);
    btec->fireBookChange(msg);
    ASSERT_TRUE(tb->valid());
    ASSERT_FALSE(tb->heldValue());
 } 

#ifdef REPLAY_BUILD
TEST_F(test_protection_adjusters, time_thru_book) {
    std::string symbol{"BTEC:US5Y"};
    auto btec = g->add<MockEventSourceMarketData>(symbol);
    ASSERT_TRUE((btec));
 
    NiceMock<MockTheo> valuation(g, symbol, btec);
    
    std::chrono::seconds two_seconds{2};
    auto tb = g->add<TimeThruBook>(&valuation, 1, two_seconds);
    ASSERT_TRUE((tb));
    ASSERT_FALSE((tb->valid()));

    md::Book b;
    NiceMock<MockBookFiniteDepthMsg> msg;
    msg.setOutrightBook(&b);
    btec->fireBookChange(msg);
    
    ASSERT_FALSE(tb->valid());

    b.insert(md::Order{1001, Side::Bid, 1000, 100.0});
    b.insert(md::Order{2001, Side::Ask, 1000, 101.0});
    valuation.setValue(100.5);
    
    btec->fireBookChange(msg);
    ASSERT_TRUE(tb->valid());
    ASSERT_FALSE(tb->heldValue());

    valuation.setValue(101 + 1.49);
    btec->fireBookChange(msg);
    ASSERT_TRUE(tb->valid());
    ASSERT_FALSE(tb->heldValue());
    
    //let 1 second pass and fire again... should not tick yet
    std::chrono::nanoseconds sleep_time;
    clock_override clock;
    sleep_time = std::chrono::seconds{1};
    clock.incrementTime(sleep_time);
    btec->fireBookChange(msg);
    ASSERT_TRUE(tb->valid());
    ASSERT_FALSE(tb->heldValue());

    //let 4 second pass and fire again... now it should tick
    sleep_time = std::chrono::seconds{4};
    clock.incrementTime(sleep_time);
    btec->fireBookChange(msg);
    ASSERT_TRUE(tb->valid());
    ASSERT_TRUE(tb->heldValue());

    //.49 ticks outside ask; clock should now not tick
    valuation.setValue(101 + 0.49);
    btec->fireBookChange(msg);
    ASSERT_TRUE(tb->valid());
    ASSERT_FALSE(tb->heldValue());
 } 
#endif

