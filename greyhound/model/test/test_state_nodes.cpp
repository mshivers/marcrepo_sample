#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "model/test/mock_bookmsg.h"
#include "model/test/mock_event_source_market_data.h"
#include "model/test/clock_override.h"

#include "model/util_nodes.h"
#include "model/state_nodes.h"
#include <vhl/IvBookFiniteDepthMsg.hpp>

#include <vpl/Price.hpp>
#include <vhl/IvFiniteDepthBook.hpp>
#include <vhl/IvBookTradeMsg.hpp>
#include <vhl/IvBookOrderUpdateMsg.hpp>
#include <vhl/IvBookLevelUpdateMsg.hpp>

#include "model/test/utils.h"
#include "model/test/mock_event_source_market_data.h"

// tests for theo signals.

using testing::ReturnRef;
using testing::NiceMock;

struct test_state_nodes : public ::testing::Test, TestGraph
{
    test_state_nodes() : TestGraph("NASDAQ:AAPL", 100)
                       , md(g->add<MockEventSourceMarketData>("NASDAQ:AAPL"))
    {
        msg.setOutrightBook(&b);
    }
    NiceMock<MockBookFiniteDepthMsg> msg;
    MockEventSourceMarketData * md;
    md::Book b;
    clock_override clock;
};

TEST_F(test_state_nodes, test_trade_side_liquidity) {
    auto size_liquidity = g->add<TradeSideLiquidity>(md, false);
    auto count_liquidity = g->add<TradeSideLiquidity>(md, true);
    ASSERT_TRUE((size_liquidity));
    ASSERT_TRUE((count_liquidity));

    // create some market data.
    b.insert(md::Order{1001, Side::Bid, 100, 10.0});
    b.insert(md::Order{1002, Side::Bid, 200, 10.0});
    b.insert(md::Order{1003, Side::Bid, 400, 10.0});
    b.insert(md::Order{2001, Side::Ask, 200, 11.0});
    md->fireBookChange(msg);

    msg.addTrade(MockBookTradeMsg{200, 10});
    md->fireBookChange(msg);

    ASSERT_EQ(size_liquidity->value(), 700);
    ASSERT_EQ(count_liquidity->value(), 3);
    
    //doesn't fire on quotes
    msg.clearTrades();
    b.insert(md::Order{2002, Side::Ask, 400, 11.0});
    md->fireBookChange(msg);
    ASSERT_FALSE((size_liquidity->ticked()));
    ASSERT_FALSE((count_liquidity->ticked()));

    msg.addTrade(MockBookTradeMsg{300, 11});
    md->fireBookChange(msg);

    ASSERT_EQ(size_liquidity->value(), 600);
    ASSERT_EQ(count_liquidity->value(), 2);
}

TEST_F(test_state_nodes, test_avg_order_size) {
    auto avg_order_size = g->add<AvgOrderSize>(md);
    ASSERT_TRUE((avg_order_size));

    // create some market data.
    b.insert(md::Order{1001, Side::Bid, 100, 10.0});
    b.insert(md::Order{1002, Side::Bid, 200, 10.0});
    b.insert(md::Order{1003, Side::Bid, 400, 10.0});
    b.insert(md::Order{2001, Side::Ask, 200, 11.0});
    md->fireBookChange(msg);

    msg.addTrade(MockBookTradeMsg{200, 10});
    md->fireBookChange(msg);

    ASSERT_EQ(avg_order_size->value(), 700/3.0);
    
    //doesn't fire on quotes
    msg.clearTrades();
    b.insert(md::Order{2002, Side::Ask, 400, 11.0});
    md->fireBookChange(msg);
    ASSERT_FALSE((avg_order_size->ticked()));

    msg.addTrade(MockBookTradeMsg{300, 11});
    md->fireBookChange(msg);

    ASSERT_EQ(avg_order_size->value(), 600/2.0);
}

TEST_F(test_state_nodes, test_trade_dist) {
    auto wave = g->add<WeightAve>(md);
    ASSERT_TRUE((wave));

    auto trade_dist = g->add<TradeDist>(wave);
    ASSERT_TRUE((trade_dist));

    // create some market data.
    b.insert(md::Order{1001, Side::Bid, 200, 10.0});
    b.insert(md::Order{1002, Side::Bid, 200, 10.0});
    b.insert(md::Order{1003, Side::Bid, 400, 10.0});
    b.insert(md::Order{2001, Side::Ask, 200, 11.0});
    md->fireBookChange(msg);

    EXPECT_NEAR(wave->value(), 10.8, 0.00001);

    msg.addTrade(MockBookTradeMsg{200, 10});
    md->fireBookChange(msg);

    EXPECT_NEAR(trade_dist->value(), 0.8, 0.00001);
    
    //doesn't fire on quotes
    msg.clearTrades();
    b.cancel(1001);
    b.insert(md::Order{2002, Side::Ask, 100, 11.0});
    md->fireBookChange(msg);
    ASSERT_FALSE((trade_dist->ticked()));

    msg.addTrade(MockBookTradeMsg{300, 11});
    md->fireBookChange(msg);

    EXPECT_NEAR(wave->value(), 10.6666,  0.0001);
    EXPECT_NEAR(trade_dist->value(), 0.333333, 0.0001);
}


TEST_F(test_state_nodes, test_signed_trade_cost) {
    auto wave = g->add<WeightAve>(md);
    ASSERT_TRUE((wave));

    auto trade_cost= g->add<SignedTradeCost>(wave);
    ASSERT_TRUE((trade_cost));

    // create some market data.
    b.insert(md::Order{1001, Side::Bid, 200, 10.0});
    b.insert(md::Order{1002, Side::Bid, 200, 10.0});
    b.insert(md::Order{1003, Side::Bid, 400, 10.0});
    b.insert(md::Order{2001, Side::Ask, 200, 11.0});
    md->fireBookChange(msg);

    EXPECT_NEAR(wave->value(), 10.8, 0.00001);

    msg.addTrade(MockBookTradeMsg{200, 10});
    md->fireBookChange(msg);

    EXPECT_NEAR(trade_cost->value(), -200*0.8, 0.00001);
    
    //doesn't fire on quotes
    msg.clearTrades();
    b.cancel(1001);
    b.insert(md::Order{2002, Side::Ask, 100, 11.0});
    md->fireBookChange(msg);
    ASSERT_FALSE((trade_cost->ticked()));

    msg.addTrade(MockBookTradeMsg{300, 11});
    md->fireBookChange(msg);

    EXPECT_NEAR(wave->value(), 10.6666,  0.0001);
    EXPECT_NEAR(trade_cost->value(), 300 * 0.3333333334, 0.0001);
}

TEST_F(test_state_nodes, test_trade_aggression) {
    auto sig = g->add<WeightAve>(md);
    ASSERT_TRUE((sig));
 
    auto trade_aggression = g->add<TradeAggression>(sig, std::chrono::minutes{5});
    ASSERT_TRUE((trade_aggression));

    b.insert(md::Order{1001, Side::Bid, 200, 10.0});
    b.insert(md::Order{1002, Side::Ask, 300, 11.0});
    md->fireBookChange(msg);

    EXPECT_NEAR(sig->heldValue(), 10.4, .0001);

    msg.addTrade(MockBookTradeMsg{200, 10});
    md->fireBookChange(msg);
    ASSERT_TRUE(trade_aggression->ticked());
    //EXPECT_NEAR(trade_aggression->value(), 0, 0.00001);
    msg.clearTrades();

    msg.addTrade(MockBookTradeMsg{100, 10});
    md->fireBookChange(msg);
    EXPECT_NEAR(trade_aggression->value(), -1*(-200*0.4), 0.001);
    msg.clearTrades();

    msg.addTrade(MockBookTradeMsg{200, 11});
    md->fireBookChange(msg);
    EXPECT_NEAR(trade_aggression->value(), -200*0.4-100*0.4, 0.001);
    msg.clearTrades();

    msg.addTrade(MockBookTradeMsg{100, 10});
    md->fireBookChange(msg);
    EXPECT_NEAR(trade_aggression->value(), -1*(-200*0.4-100*0.4 +200*0.6), 0.001);
    msg.clearTrades();

    msg.addTrade(MockBookTradeMsg{100, 10});
    md->fireBookChange(msg);
    EXPECT_NEAR(trade_aggression->value(), -1*(-200*0.4-100*0.4 +200*0.6 -100*0.4), 0.001);
    msg.clearTrades();

    b.insert(md::Order{1003, Side::Ask, 300, 11.0});
    md->fireBookChange(msg);
    ASSERT_TRUE((trade_aggression->ticked()));
}

TEST_F(test_state_nodes, test_trade_momentum) {
    auto trade_momentum_count = g->add<TradeMomentum>(md, std::chrono::minutes{5}, true);
    auto trade_momentum_size = g->add<TradeMomentum>(md, std::chrono::minutes{5}, false);
    ASSERT_TRUE((trade_momentum_count));
    ASSERT_TRUE((trade_momentum_size));

    b.insert(md::Order{1001, Side::Bid, 200, 10.0});
    b.insert(md::Order{1002, Side::Ask, 300, 11.0});
    md->fireBookChange(msg);

    msg.addTrade(MockBookTradeMsg{200, 10});
    md->fireBookChange(msg);
    //needs to tick twice to be valid: once for Pad, then for Last
    ASSERT_TRUE(trade_momentum_count->valid());
    ASSERT_TRUE(trade_momentum_size->valid());
    ASSERT_TRUE(trade_momentum_count->ticked());
    ASSERT_TRUE(trade_momentum_size->ticked());
    //EXPECT_NEAR(trade_momentum_count->value(), 0, 0.00001);
    //EXPECT_NEAR(trade_momentum_size->value(), 0, 0.00001);
    msg.clearTrades();

    msg.addTrade(MockBookTradeMsg{100, 10});
    md->fireBookChange(msg);
    EXPECT_NEAR(trade_momentum_count->value(), -1*(-1), 0.001);
    EXPECT_NEAR(trade_momentum_size->value(), -1*(-200), 0.001);
    msg.clearTrades();

    msg.addTrade(MockBookTradeMsg{200, 11});
    md->fireBookChange(msg);
    EXPECT_NEAR(trade_momentum_count->value(), 1*(-2), 0.001);
    EXPECT_NEAR(trade_momentum_size->value(), 1*(-300), 0.001);
    msg.clearTrades();

    msg.addTrade(MockBookTradeMsg{100, 10});
    md->fireBookChange(msg);
    EXPECT_NEAR(trade_momentum_count->value(), -1*(-1), 0.001);
    EXPECT_NEAR(trade_momentum_size->value(), -1*(-100), 0.001);
    msg.clearTrades();
   
    b.insert(md::Order{1003, Side::Ask, 300, 11.0});
    md->fireBookChange(msg);
    ASSERT_TRUE((trade_momentum_count->ticked()));
    ASSERT_TRUE((trade_momentum_size->ticked()));
}


