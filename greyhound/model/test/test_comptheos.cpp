#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "model/comptheos.h"
#include "model/market_data.h"
#include "model/theos.h"
#include "model/strategy.h"
#include "model/test/mock_bookmsg.h"
#include "model/test/clock_override.h"
#include "model/test/mock_event_source_market_data.h"

#include "model/test/utils.h"

using testing::NiceMock;

// tests for theo signals.

using Units = ValueNode::Units;


struct test_comptheos : public ::testing::Test, TestGraphMultiSym {
    NiceMock<MockBookFiniteDepthMsg> btec_msg, espeed_msg, base_msg, ref_msg;
    md::Book btec_book, espeed_book, base_book, ref_book;
    test_comptheos() : TestGraphMultiSym({"ESPEED:US10Y", "BTEC:US10Y", "BTEC:US5Y"},
                                         {100, 100, 100})
                     , espeed(g->add<MockEventSourceMarketData>("ESPEED:US10Y"))
                     , btec(g->add<MockEventSourceMarketData>("BTEC:US10Y"))
                     , btec5y(g->add<MockEventSourceMarketData>("BTEC:US5Y"))
    {
        btec_msg.setOutrightBook(&btec_book);
        espeed_msg.setOutrightBook(&espeed_book);
        base_msg.setOutrightBook(&base_book);
        ref_msg.setOutrightBook(&ref_book);
    }
    MockEventSourceMarketData *espeed, *btec, *btec5y;
    clock_override clock;
};

TEST_F(test_comptheos, comptheo) {
    auto espeed_bbot = g->add<OnBBOT>(espeed);

    ASSERT_TRUE((espeed));
    ASSERT_TRUE((btec));
    ASSERT_TRUE((espeed_bbot));

    auto base = g->add<Midpt>(btec);
    auto ref = g->add<WeightAve>(espeed);

    double length = 10;
    auto base_ema = g->add<TickEMA>(base, btec, length);
    auto ref_ema = g->add<TickEMA>(ref, espeed, length);
    double vol_mult = 1.0;
    auto comp_theo = g->add<CompTheo>(ref, ref_ema, base_ema, vol_mult);

    ASSERT_TRUE((base));
    ASSERT_TRUE((ref));
    ASSERT_TRUE((comp_theo));
    ASSERT_TRUE((comp_theo->hasParent(ref)));
    ASSERT_TRUE((comp_theo->hasParent(ref_ema)));
    ASSERT_TRUE((comp_theo->hasParent(base_ema)));

    //ref: espeed weightave: 10.75
    espeed_book.insert(md::Order{1001, Side::Bid, 300, 10.0});
    espeed_book.insert(md::Order{1002, Side::Ask, 100, 11.0});

    espeed->fireBookChange(espeed_msg);

    ASSERT_FALSE((base->ticked()));
    ASSERT_TRUE((ref->ticked()));
    EXPECT_DOUBLE_EQ(ref->value(), 10.75);

    // base: btec midpt: 10.5
    btec_book.insert(md::Order{1001, Side::Bid, 100, 10.0});
    btec_book.insert(md::Order{1002, Side::Ask, 200, 11.0});

    btec->fireBookChange(btec_msg);

    ASSERT_TRUE((base->ticked()));
    EXPECT_NEAR(base->value(), 10.5, .000001);
    ASSERT_FALSE((ref->ticked()));

    // 10.5 * (10.75/10.75)
    ASSERT_TRUE((comp_theo->ticked()));
    EXPECT_EQ(comp_theo->value(), 10.5);

    // ref: btec midpt: 10.75
    btec_book.insert(md::Order{1003, Side::Bid, 300, 10.5});

    btec->fireBookChange(btec_msg);

    ASSERT_TRUE((base->ticked()));
    ASSERT_TRUE((comp_theo->ticked()));
    EXPECT_NEAR(comp_theo->value(), 10.625, .00001);
}

#ifdef REPLAY_BUILD
TEST_F(test_comptheos, comptheo_wrappers) {
    double volMult = 1.0;
    std::chrono::minutes length{30};
    auto espeed_wave = g->add<WeightAve>(espeed);
    auto btec_wave = g->add<WeightAve>(btec);
    auto timeCT = g->add<TimeCompTheo>(espeed_wave, btec_wave, length, volMult);

    double tick_length = 10;
    auto tickCT = g->add<TickCompTheo>(espeed_wave, btec_wave, tick_length, volMult);

    ASSERT_TRUE((tickCT));
    ASSERT_TRUE((timeCT));

    // base: espeed weightave: 10.5
    espeed_book.insert(md::Order{1001, Side::Bid, 100, 10.0});
    espeed_book.insert(md::Order{1002, Side::Ask, 100, 11.0});

    espeed->fireBookChange(espeed_msg);
    //espeed is base theo, for timeCT, base_ema will tick
    ASSERT_TRUE(timeCT->ticked());
    ASSERT_TRUE(tickCT->ticked());
    
    // ref: btec weightave: 10.333
    btec_book.insert(md::Order{1001, Side::Bid, 100, 10.0});
    btec_book.insert(md::Order{1002, Side::Ask, 200, 11.0});

    btec->fireBookChange(btec_msg);
    //btec is ref; when ref_ema ticks, all CTs should tick
    ASSERT_TRUE(timeCT->ticked());
    ASSERT_TRUE(tickCT->ticked());
}
#endif

#ifdef REPLAY_BUILD
TEST_F(test_comptheos, ref_trade_intensity) {
    std::string base{"BTEC:US5Y"};
    std::string ref{"BTEC:US10Y"};
    std::chrono::minutes long_ave{30};
    using millis = std::chrono::milliseconds;
    using nanos = std::chrono::nanoseconds;

    auto base_md = g->add<MockEventSourceMarketData>(base);
    auto ref_md = g->add<MockEventSourceMarketData>(ref);

    nanos short_ave = millis{200};
    auto rti = g->add<RefTradeIntensity>(base_md, ref_md, long_ave, short_ave);
    ASSERT_TRUE((rti));
    ASSERT_FALSE((rti->valid()));
    
    //initial both order books
    base_book.insert(md::Order{1001, Side::Bid, 100, 99.0});
    base_book.insert(md::Order{2001, Side::Ask, 100, 101.0});
    base_md->fireBookChange(ref_msg);
    ref_book.insert(md::Order{1001, Side::Bid, 100, 105.0});
    ref_book.insert(md::Order{2001, Side::Ask, 100, 107.0});
    ref_md->fireBookChange(ref_msg);
 
    //long sum = short sum = 2 => base intensity = 1
    base_msg.addTrade(MockBookTradeMsg{2, 101});
    //long sum = short sum = 10 => ref intensity = 1
    ref_msg.addTrade(MockBookTradeMsg{10, 107});

    //rti and all sums fire on joint clock, so this will update rti twice
    base_md->fireBookChange(base_msg);
    ref_md->fireBookChange(ref_msg);
    base_msg.clearTrades();
    ref_msg.clearTrades();

    ASSERT_TRUE(rti->ticked());
    ASSERT_NEAR(rti->value(), 0.5, 0.01);

    Int64 start;
    double decay, elapsed, ref_ti, base_ti;
    
    // sleep for decay
    start = g->nSecUptime();
    clock.incrementTime(millis{100}); // halflife of short sum
    
    //add a new trade
    //ref long sum ~= 10+5=15; ref short sum ~= 10/2+5=10 > ref intensity = 2/3 
    //base long sum ~= 2; base short sum ~= 2/2=1 > base intensity = 1/2
    //rti ~= 2/3 / 7/6 = 4/7 ~= 0.5714
    ref_msg.addTrade(MockBookTradeMsg{5, 107});
    ref_md->fireBookChange(ref_msg);

    elapsed = g->nSecUptime() - start;
    decay = short_ave.count() / elapsed;
    if ( decay < 1 ) decay = 1;
    ref_ti = (10/decay + 5)/15.0;  //~~2/3 
    base_ti = (2/decay)/2.0;  //~~1/2 
 
    ASSERT_NEAR(rti->value(), (ref_ti)/(ref_ti+base_ti), 0.01);
    ref_msg.clearTrades();

    // sleep for decay, full short decay
    clock.incrementTime(millis{210}); // full decay of short sum
    
    base_msg.addTrade(MockBookTradeMsg{12, 99});
    base_md->fireBookChange(base_msg);
    base_msg.clearTrades();

    //ref short sum = 0, so rti=0;
    ASSERT_EQ(rti->value(), 0);

    // sleep for decay
    clock.incrementTime(millis{210}); // full decay of short sum
    
    ref_msg.addTrade(MockBookTradeMsg{1, 105});
    ref_md->fireBookChange(ref_msg);

    //ref short sum = 0, so rti=0;
    ASSERT_EQ(rti->value(), 1);

    //fire without trades should not change rti (base and ref decay by same amount)
    ref_msg.clearTrades();
    ref_md->fireBookChange(ref_msg);  
    ASSERT_FALSE(rti->ticked()); //doesn't tick on quotes, only trades
}

TEST_F(test_comptheos, test_TICT) {
    std::string base{"BTEC:US5Y"};
    std::string ref{"BTEC:US10Y"};

    auto base_md = g->add<MockEventSourceMarketData>(base);
    auto ref_md = g->add<MockEventSourceMarketData>(ref);

    auto base_mid = g->add<Midpt>(base_md);
    auto ref_wave = g->add<WeightAve>(ref_md);
    ASSERT_TRUE((base_mid));
    ASSERT_TRUE((ref_wave));

    using millis = std::chrono::milliseconds;
    std::chrono::minutes long_ave{30};
    std::chrono::nanoseconds short_ave = millis{200};
    double intensity_mult=10, vol_mult=2;
    auto tict = g->add<TradeIntensityCompTheo>(base_mid, ref_wave, 
            long_ave, short_ave, intensity_mult, vol_mult);
    ASSERT_TRUE((tict));
    
    //initialize both order books
    base_book.insert(md::Order{1001, Side::Bid, 100, 99.0});
    base_book.insert(md::Order{2001, Side::Ask, 100, 101.0});
    base_md->fireBookChange(base_msg);
    ref_book.insert(md::Order{1001, Side::Bid, 100, 105.0});
    ref_book.insert(md::Order{2001, Side::Ask, 100, 107.0});
    ref_md->fireBookChange(ref_msg);
 
    //rti needs to see a trade to fire
    //long sum = short sum = 2 => base intensity = 1
    base_msg.addTrade(MockBookTradeMsg{2, 101});
    base_md->fireBookChange(base_msg);  
    base_msg.clearTrades();

    //long sum = short sum = 10 => ref intensity = 1
    ref_msg.addTrade(MockBookTradeMsg{10, 107});
    ref_md->fireBookChange(ref_msg);  
    ref_msg.clearTrades();

    ASSERT_TRUE(tict->ticked());
    ASSERT_TRUE(tict->valid());
    //initially ref_theo==ref_ema so tict = base_ema(=base_theo)=100
    ASSERT_EQ(tict->value(), 100);

    // sleep for decay
    Int64 start;
    double decay, elapsed, ref_ti, base_ti;
    start = g->nSecUptime();
    clock.incrementTime(millis{100}); // halflife of short sum
     
    //add a new trade
    //ref long sum ~= 10+5=15; ref short sum ~= 10/2+5=10 > ref intensity = 2/3 
    //base long sum ~= 2; base short sum ~= 2/2=1 > base intensity = 1/2
    //rti ~= 2/3 / 7/6 = 4/7 ~= 0.5714
   
    ref_book.insert(md::Order{1002, Side::Bid, 100, 106.0});
    //ref_wave changes to 106.5 from 106, so the ref_ema will update
    ref_msg.addTrade(MockBookTradeMsg{5, 107});
    ref_md->fireBookChange(ref_msg);
    ref_msg.clearTrades();

    elapsed = g->nSecUptime() - start;
    decay = short_ave.count() / elapsed;
    if ( decay < 1 ) decay = 1;
    ref_ti = (10/decay + 5)/15.0;  //~~2/3 
    base_ti = (2/decay)/2.0;  //~~1/2 
    double decay_len = intensity_mult * (ref_ti)/(ref_ti+base_ti);
    //intensity mult = 10, so all ema lengths are im*rti = 5.714 
    //new ref_ema = 106 + (106.5-106)/5.714 = 106.0875 
    auto ref_ema = tict->ref_ema_->value();
    ASSERT_NEAR(ref_ema, 106.0 + 0.5/decay_len, 0.01);    
    ASSERT_NEAR(tict->value(), 100*std::pow(106.5/ref_ema, vol_mult), 0.01);

    // sleep for decay
    clock.incrementTime(millis{210}); // full decay of short sum
    
    //new base mid is 100.5
    base_book.insert(md::Order{1002, Side::Bid, 100, 100.0});
    base_msg.addTrade(MockBookTradeMsg{12, 99});
    base_md->fireBookChange(base_msg);
    base_msg.clearTrades();

    //ref short sum = 0, so rti=0; so all emas update to current value
    ASSERT_EQ(tict->ref_ema_->heldValue(), tict->ref_theo_->heldValue());
    ASSERT_EQ(tict->base_ema_->value(), tict->base_theo_->value());
    ASSERT_EQ(tict->value(), tict->base_theo_->value());

    // sleep for decay
    clock.incrementTime(millis{210}); // full decay of short sum
    
    //fire without trades should not change tict (base and ref decay by same amount)
    ref_msg.clearTrades();
    ref_book.insert(md::Order{1003, Side::Bid, 100, 107.0}); //ref_theo = 107 locked
    ref_md->fireBookChange(ref_msg);  
    ASSERT_TRUE(tict->ticked()); //doesn't tick on quotes, only trades
    ASSERT_EQ(tict->value(), tict->base_theo_->heldValue()); //rti is zero, all emas are current theo
    ASSERT_EQ(tict->value(), 100.5);

    base_book.insert(md::Order{1003, Side::Bid, 100, 101.0}); //base theo is 101 locked
    base_md->fireBookChange(base_msg);  
    ASSERT_TRUE(tict->ticked()); 
    ASSERT_EQ(tict->base_theo_->value(), 101); 
    ASSERT_EQ(tict->value(), tict->base_theo_->value());

}
#endif

TEST_F(test_comptheos, test_vwap_comptheo) {
    std::string base_symbol{"BTEC:US5Y"};
    std::string ref_symbol{"BTEC:US10Y"};

    auto base_md = g->add<MockEventSourceMarketData>(base_symbol);
    auto ref_md = g->add<MockEventSourceMarketData>(ref_symbol);
    
    auto ref_mid = g->add<Midpt>(ref_md);
    double length{10}; 
    double vol_mult{2.0};
    auto vwapct = g->add<TickVWAPCompTheo>(base_md, ref_mid, length, vol_mult);
    ASSERT_TRUE((vwapct));

    /*
    ON_CALL(base_msg, tickSize())
        .WillByDefault(ReturnRef(one_cent_tick_size));
    ON_CALL(ref_msg, tickSize())
        .WillByDefault(ReturnRef(one_cent_tick_size));
        */

    // create some market data.
    base_book.insert(md::Order{1001, Side::Bid, 400, 100.0});
    base_book.insert(md::Order{2001, Side::Ask, 300, 110.0});
    
    ref_book.insert(md::Order{1001, Side::Bid, 50, 92.0});
    ref_book.insert(md::Order{2001, Side::Ask, 70, 94.0});
    
    base_md->fireBookChange(base_msg);
    ASSERT_FALSE(vwapct->ticked()); //doesn't fire on base non-trades
    ref_md->fireBookChange(ref_msg);
    ASSERT_TRUE(vwapct->ticked()); //fires on ref theo updates
    ASSERT_FALSE(vwapct->valid()); //no trades yet
  
    base_msg.addTrade(MockBookTradeMsg{5, 110});
    base_md->fireBookChange(base_msg);
    base_msg.clearTrades();
    ASSERT_FALSE(vwapct->valid());  // still no ref trade
    ASSERT_TRUE(vwapct->base_vwap_->ticked()); 
    ASSERT_EQ(vwapct->base_vwap_->value(), 110); 
 
    ref_msg.addTrade(MockBookTradeMsg{2, 94});
    ref_md->fireBookChange(ref_msg);
    ref_msg.clearTrades();

    ASSERT_TRUE(vwapct->ticked()); 
    ASSERT_TRUE(vwapct->valid());  // everything has traded once
    ASSERT_EQ(vwapct->value(), 110 * std::pow(93.0/94.0, vol_mult)); //==base_theo  

    ref_msg.addTrade(MockBookTradeMsg{20, 92});
    ref_md->fireBookChange(ref_msg);
    ref_msg.clearTrades();
    ASSERT_TRUE(vwapct->ticked());
    auto ref_vwap = vwapct->ref_vwap_->value();
    ASSERT_GT(ref_vwap, 92);
    ASSERT_LT(ref_vwap, 94);
    ASSERT_EQ(vwapct->base_vwap_->heldValue(), 110);
    ASSERT_EQ(vwapct->ref_theo_->value(), 93);
    ASSERT_NEAR(vwapct->value(), 110 * std::pow(93/ref_vwap,vol_mult), 0.0001);

    base_msg.addTrade(MockBookTradeMsg{1, 100});
    base_md->fireBookChange(base_msg);
    base_msg.clearTrades();
    auto base_vwap = vwapct->base_vwap_->value();
    ref_vwap = vwapct->ref_vwap_->heldValue();
    ASSERT_GT(base_vwap, 100);
    ASSERT_LT(base_vwap, 110);
    ASSERT_FALSE(vwapct->ref_vwap_->ticked()); //vwaps don't tick on each others trades
    ASSERT_NEAR(vwapct->value(), base_vwap * std::pow(93/ref_vwap,vol_mult), 0.0001);

    base_book.insert(md::Order{1002, Side::Bid, 400, 105.0}); //change base theo
    base_md->fireBookChange(base_msg);
    ASSERT_FALSE(vwapct->ticked());
}


