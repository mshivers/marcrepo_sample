#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "mock_bookmsg.h"
#include "model/util_nodes.h"
#include "model/theos.h"
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

struct test_theos : public ::testing::Test, TestGraphMultiSym {
    NiceMock<MockBookFiniteDepthMsg> msg;
    md::Book b;
    MockEventSourceMarketData* md;
    test_theos() : TestGraphMultiSym({"NASDAQ:AAPL", "BTEC:US10Y", "BTEC:US2Y"}, {1., 1., 1.})
                 , md(g->add<MockEventSourceMarketData>("NASDAQ:AAPL"))
    {
        msg.setOutrightBook(&b);
    }
};

TEST_F(test_theos, test_midpt) {
    auto sig = g->add<Midpt>(md);
    ASSERT_TRUE((sig));

    auto bbot = g->add<OnBBOT>(md);
    ASSERT_TRUE((bbot));
    ASSERT_TRUE((sig->getClock()==bbot));

    // create some market data.
    b.insert(md::Order{1001, Side::Bid, 100, 10.0});
    b.insert(md::Order{1002, Side::Ask, 200, 11.0});
    md->fireBookChange(msg);

    EXPECT_NEAR(sig->heldValue(), 10.5, .0001);

    // insert some more size on the ask size.
    b.insert(md::Order{1003, Side::Ask, 300, 11.0});
    md->fireBookChange(msg);

    // should not have changed anything.
    EXPECT_NEAR(sig->heldValue(), 10.5, .0001);
}

TEST_F(test_theos, test_weightave) {
    auto sig = g->add<WeightAve>(md);
    ASSERT_TRUE((sig));

    auto bbot = g->add<OnBBOT>(md);
    ASSERT_TRUE((bbot));
    ASSERT_TRUE((sig->getClock()==bbot));
    ASSERT_EQ(sig->numParents(), 0u);

    b.insert(md::Order{1001, Side::Bid, 100, 10.0});
    b.insert(md::Order{1002, Side::Ask, 200, 11.0});
    md->fireBookChange(msg);

    EXPECT_NEAR(sig->heldValue(), 10.3333, .0001);

    b.insert(md::Order{1003, Side::Ask, 300, 11.0});
    md->fireBookChange(msg);

    EXPECT_NEAR(sig->heldValue(), 10.1666, .0001);
}

TEST_F(test_theos, test_bookDepth) {

    b.insert(md::Order{1001, Side::Ask, 5, 10.0});
    md->fireBookChange(msg);
    EXPECT_EQ(1u, md->depth());
        
    b.insert(md::Order{1002, Side::Ask, 5, 12.0});
    md->fireBookChange(msg);
    EXPECT_EQ(2u, md->depth());

    b.insert(md::Order{1003, Side::Bid, 5, 5.0});
    md->fireBookChange(msg);
    EXPECT_EQ(2u, md->depth());

    b.cancel(1002);
    md->fireBookChange(msg);
    EXPECT_EQ(1u, md->depth());
 }

TEST_F(test_theos, test_FillAve) {
    bool use_counts = true;
    size_t max_depth = 2;
    auto sig = g->add<FillAve>(md, 2, 0.5, 1000, 
            max_depth, use_counts);
    ASSERT_TRUE((sig));
    ASSERT_EQ(sig->numParents(), 3u);

    b.insert(md::Order{1001, Side::Bid, 10, 9.0});
    b.insert(md::Order{2001, Side::Ask, 3, 10.0});
    b.insert(md::Order{2002, Side::Ask, 7, 12.0});

    md->fireBookChange(msg);
    EXPECT_GT(sig->value(), 9.0);
    EXPECT_LT(sig->value(), 10.0);
}

TEST_F(test_theos, test_priceToFill_use_counts) {
    auto size = g->add<Const>(3.0);
    bool use_counts = true;
    size_t max_depth = 2;
    auto sig = g->add<PriceToFill>(md, ::Side::Ask, size,
            max_depth, use_counts);
    ASSERT_TRUE((sig));

    b.insert(md::Order{1001, Side::Bid, 10, 5.0});
    b.insert(md::Order{2001, Side::Ask, 3, 10.0});
    b.insert(md::Order{2002, Side::Ask, 7, 12.0});

    md->fireBookChange(msg);
    //find the remaining size in the missing price point 2-deep at 11.
    EXPECT_NEAR(sig->heldValue(), 1*10 + 2*11, .0001);
   
    b.insert(md::Order{2003, Side::Ask, 7, 10.0});
    md->fireBookChange(msg);
    EXPECT_NEAR(sig->heldValue(), 2 * 10 + 1*11 , .0001);

    // find all size in the first level
    b.insert(md::Order{2004, Side::Ask, 2, 10.0});
    md->fireBookChange(msg);
    EXPECT_NEAR(sig->heldValue(), 3 * 10, .0001);
}

TEST_F(test_theos, test_priceToFill) {
    auto size = g->add<Const>(10.0);
    bool use_counts = false;
    size_t max_depth = 2;
    auto sig = g->add<PriceToFill>(md, ::Side::Ask, size,
            max_depth, use_counts);
    ASSERT_TRUE((sig));

    b.insert(md::Order{1001, Side::Bid, 10, 5.0});
    b.insert(md::Order{2001, Side::Ask, 3, 10.0});
    b.insert(md::Order{2002, Side::Ask, 7, 12.0});

    md->fireBookChange(msg);
    //find the remaining size in the missing price point 2-deep at 11.
    EXPECT_NEAR(sig->heldValue(), 3*10 + 7*11, .0001);
   
    b.insert(md::Order{2003, Side::Ask, 7, 10.0});
    md->fireBookChange(msg);
    EXPECT_NEAR(sig->heldValue(), 10 * 10 , .0001);
}

TEST_F(test_theos, test_priceToFill_missingSizeIsAtNextLevel) {
    double size = 10;
    auto sizeSig = g->add<Const>(size);
    bool use_counts = false; 
    size_t max_depth = 2;  
    auto sigAsk = g->add<PriceToFill>( md, ::Side::Ask,
            sizeSig, max_depth, use_counts);
    ASSERT_TRUE((sigAsk));
    auto sigBid = g->add<PriceToFill>( md, ::Side::Bid,
            sizeSig, max_depth, use_counts);
    ASSERT_TRUE((sigBid));

    b.insert(md::Order{1001, Side::Bid, 5, 10.0});
    b.insert(md::Order{1002, Side::Ask, 5,  12.0});

    md->fireBookChange(msg);
    
    //ticksize is 1.0; should assume extra 5 is at price 9 & 13, resp.
    EXPECT_EQ(b.depth(), 1u); //confirm book depth is 1-based, not 0-based.
    EXPECT_EQ(sigAsk->heldValue(), 125);
    EXPECT_EQ(sigBid->heldValue(), 95);
}


TEST_F(test_theos, priceToFill_split_book)
{
    auto* size = g->add<Const>(10);
    
    SplitMarketData::Config config;
    config.metaDataEnd_.initial_qty = 6;
    auto* md_split = g->add<SplitMarketData>("NASDAQ:AAPL", config);
    auto* ptf = g->add<PriceToFill>(md_split, Side::Ask, size, 5, false);
    
    auto* raw = g->add<RawMarketData>("NASDAQ:AAPL");
    auto* ptf_raw = g->add<PriceToFill>(raw, Side::Ask, size, 5, false);

    b.insert(md::Order{1000, Side::Bid, 5, 99.0});
    b.insert(md::Order{1001, Side::Ask, 5, 100.0});
    b.insert(md::Order{1002, Side::Ask, 10, 100.0});
    b.insert(md::Order{1003, Side::Ask, 5, 101.0});

    md->fireBookChange(msg);
    
    EXPECT_EQ(ptf_raw->heldValue(), 10 * 100);
    EXPECT_EQ(ptf->heldValue(), 5*100 + 5*101);
}


TEST_F(test_theos, priceToFill_split_book_has_empty_price_level)
{
    auto* size = g->add<Const>(10);
    
    SplitMarketData::Config config;
    config.metaDataEnd_.initial_qty = 6;
    auto* md_split = g->add<SplitMarketData>("NASDAQ:AAPL", config);
    auto* ptf = g->add<PriceToFill>(md_split, Side::Ask, size, 5, false);
    
    auto* raw = g->add<RawMarketData>("NASDAQ:AAPL");
    auto* ptf_raw = g->add<PriceToFill>(raw, Side::Ask, size, 5, false);

    b.insert(md::Order{1000, Side::Bid, 5, 99.0});
    b.insert(md::Order{1001, Side::Ask, 10, 100.0});
    b.insert(md::Order{1002, Side::Ask, 10, 100.0});
    b.insert(md::Order{1003, Side::Ask, 5, 101.0});
    b.insert(md::Order{1004, Side::Ask, 5, 101.0});

    md->fireBookChange(msg);
    
    EXPECT_EQ(ptf_raw->heldValue(), 10 * 100);
    EXPECT_EQ(ptf->heldValue(), 10*101);
}



TEST_F(test_theos, priceToFill_split_book_not_enough_size)
{
    auto* size = g->add<Const>(10);
    int maxDepth = 2;
    
    SplitMarketData::Config config;
    config.metaDataEnd_.initial_qty = 6;
    auto* md_split = g->add<SplitMarketData>("NASDAQ:AAPL", config);
    auto* ptf = g->add<PriceToFill>(md_split, Side::Ask, size, maxDepth, false);

    int id=1000;
    b.insert(md::Order{id++, Side::Bid, 5, 99.0});
    b.insert(md::Order{id++, Side::Ask, 5, 100.0});
    b.insert(md::Order{id++, Side::Ask, 10, 101.0});
    b.insert(md::Order{id++, Side::Ask, 10, 102.0});
    b.insert(md::Order{id++, Side::Ask, 10, 102.0});

    md->fireBookChange(msg);
    
    EXPECT_EQ(ptf->heldValue(), 5* 100 + 5*101);
}

TEST_F(test_theos, test_AvgPriceExec_use_counts) {
    double max_depth = 2;
    auto size= g->add<Const>(4);
    bool use_counts = true; 
    auto sig = g->add<AvgPriceExec>(md, size, max_depth, use_counts);

    ASSERT_TRUE((sig));

    b.insert(md::Order{1001, Side::Bid, 5, 9.0});
    b.insert(md::Order{1002, Side::Ask, 5, 10.0});
    b.insert(md::Order{1003, Side::Ask, 1, 10.0});

    md->fireBookChange(msg);
    //std::cout << sig->bid_fill_price_->heldValue() << std::endl;
    //std::cout << sig->ask_fill_price_->heldValue() << std::endl;
    
    //bid pricetofill = 8.25: 1 at 9 and 3 at 8
    //ask pricetofill = 10.5: 2 at 10 and 2 at 11
    EXPECT_NEAR(sig->heldValue(), 9.375, .0001);

    b.insert(md::Order{1004, Side::Bid, 2, 9.0});
    b.insert(md::Order{1005, Side::Ask, 5, 11.0}); 
    md->fireBookChange(msg);

    /*
    std::cout << b.depth() << std::endl;
    for (size_t i=0; i<b.depth(); ++i) {
        std::cout << "bidcount" << i << ": " << b.bidNumOrdersPtr()[i] << std::endl;
        std::cout << "askcount" << i << ": " << b.askNumOrdersPtr()[i] << std::endl;
    }
    std::cout << "-----" << std::endl;
    std::cout << sig->bid_fill_price_->heldValue() << std::endl;
    std::cout << sig->ask_fill_price_->heldValue() << std::endl;
    */
    
    //bid pricetofill = 8.5: 2 at 9 and 2 at 8
    //ask pricetofill = 10.5: 2 at 10 and 2 at 11
    EXPECT_NEAR(sig->heldValue(), 9.5, .0001);
}


TEST_F(test_theos, test_AvgPriceExec) {
    double max_depth = 2;
    auto size= g->add<Const>(10);
    bool use_counts = false; 
    auto sig = g->add<AvgPriceExec>(md, size, max_depth, use_counts);

    ASSERT_TRUE((sig));

    b.insert(md::Order{1001, Side::Bid, 5, 9.0});
    b.insert(md::Order{1002, Side::Ask, 5, 10.0});

    md->fireBookChange(msg);
    EXPECT_NEAR(sig->heldValue(), 9.5, .0001);

    /*
    std::cerr << "-----" << std::endl;
    std::cerr << sig->bid_fill_price_->heldValue() << std::endl;
    std::cerr << sig->ask_fill_price_->heldValue() << std::endl;
    */
    
    b.insert(md::Order{1003, Side::Bid, 5, 9.0});
    b.insert(md::Order{1004, Side::Ask, 5, 11.0});
    md->fireBookChange(msg);
    EXPECT_NEAR(sig->heldValue(), (10*9 + 5*10 + 5*11)/20.0, .0001);
}

TEST_F(test_theos, test_Const) {
    auto sig = g->add<Const>(42.0);
    EXPECT_EQ(sig->heldValue(), 42.0);
}


TEST_F(test_theos, dot_product) {
    auto wtave = g->add<WeightAve>(md);
    auto midpt = g->add<Midpt>(md);

    std::vector<ValueNode*> sigs{wtave, midpt}; 
    std::vector<double> weights{.75, .25};
    auto dot = g->add<LinearCombination>("NASDAQ:AAPL", sigs, weights);

    ASSERT_EQ(dot->numSignals(), 2u);

    // make sure the memoizer works.
    auto dot2 = g->add<LinearCombination>("NASDAQ:AAPL", sigs, weights);
    ASSERT_EQ(dot, dot2);

    // Ok, now test that it combines prices.

    b.insert(md::Order{1001, Side::Bid, 100, 10.0});
    b.insert(md::Order{1002, Side::Ask, 300, 11.0});
    md->fireBookChange(msg);

    EXPECT_EQ(midpt->heldValue(), 10.5);
    EXPECT_DOUBLE_EQ(wtave->heldValue(), 10.25);

    EXPECT_DOUBLE_EQ(dot->heldValue(), 10.5 * .25 + 10.25 * .75);
}

TEST_F(test_theos, test_sizefinder) {
    double max_depth = 2;
    double size_mult = 0.2;
    int ema_length = 2;
    auto sf = g->add<SizeFinder>(md, max_depth, size_mult, 
                                ema_length, false);
    auto sf_count = g->add<SizeFinder>(md, max_depth, size_mult, 
                                      ema_length, true);
    ASSERT_TRUE((sf));
    ASSERT_TRUE((sf_count));

    auto bd = g->add<BookDepth>(md);
    ASSERT_TRUE((bd));
    ASSERT_TRUE((md));

    b.insert(md::Order{1001, Side::Bid, 5, 9.0});
    b.insert(md::Order{1002, Side::Ask, 5, 10.0});
    b.insert(md::Order{1003, Side::Ask, 1, 10.0});
    b.insert(md::Order{1004, Side::Ask, 3, 11.0});
    b.insert(md::Order{1005, Side::Ask, 7, 12.0});

    md->fireBookChange(msg);
    /*
    std::cout << b.depth() << std::endl;
    for (size_t i=1; i<=b.depth(); ++i) {
        std::cout << "bidcount" << i << ": " << bd->bidCountToLevel(i) << std::endl;
        std::cout << "askcount" << i << ": " << bd->askCountToLevel(i) << std::endl;
        std::cout << "bidsize" << i << ": " << bd->bidSizeToLevel(i) << std::endl;
        std::cout << "asksize" << i << ": " << bd->askSizeToLevel(i) << std::endl;
    }
    */
    
    //bid size/count found = 5/1
    //ask size/count found = 9/3
    ASSERT_TRUE(sf->parentsValid());
    EXPECT_NEAR(sf->value(), 2.0, .0001); //0.2*(5+9)/2==1.4 round up to 2.0
    EXPECT_NEAR(sf_count->value(), 1.0, .0001); //0.2*(1+3)/2==0.4, round up to 1.0
    
    b.insert(md::Order{1006, Side::Bid, 20, 9.0});
    b.insert(md::Order{1007, Side::Ask, 50, 11.0}); 
    md->fireBookChange(msg);

    //bid size/count found = 25/2
    //ask size/count found = 59/4
    EXPECT_NEAR(sf->value(), std::ceil(1.4+(8.4-1.4)/2.0), .0001); //update = 0.2*(25+59)/2 = 8.4 
    EXPECT_NEAR(sf_count->value(), std::ceil(0.4+(0.6-0.4)/2.0), .0001); //update = 0.2*(2+4)/2) = 0.6
}














