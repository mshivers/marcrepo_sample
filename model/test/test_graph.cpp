#include "mock_node.h"
#include "mock_event_source_market_data.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "model/graph.h"
#include "model/market_data.h"
#include "model/theos.h"
#include "model/test/utils.h"
#include "model/test/mock_bookmsg.h"

#include <boost/range/algorithm/find.hpp>
namespace br = boost::range;

using testing::Assign;
using testing::Invoke;
using testing::_;
using testing::UnorderedElementsAre;
using ::testing::NiceMock;

struct test_graph : public ::testing::Test, TestGraphMultiSym {
    test_graph() :TestGraphMultiSym({"BTEC:US2Y", "NASDAQ:AAPL", "NASDAQ:TSLA"}, {1, 1, 1})
    {}
};

struct CreateCountNode : ValueNode
{
    static CreateCountNode* create(Graph* g)
    {
        ++nCreate;
        return new CreateCountNode(g);
    }
    static CreateCountNode* deserialize(Graph* g, const Parameters& p)
    {
        return g->add<CreateCountNode>();
    }
    
    void compute() override
    {
        status_ = StatusCode::INVALID;
    }
    
    NODE_FACTORY_MEMBERS(CreateCountNode);
    static int nCreate;
    protected:
    CreateCountNode(Graph* g) : ValueNode(g) {}
};

NODE_FACTORY_ADD(CreateCountNode);
int CreateCountNode::nCreate = 0;

TEST_F(test_graph, test_memoization) {
    CreateCountNode::nCreate = 0;

    auto n0 = g->add<CreateCountNode>();
    auto n1 = g->add<CreateCountNode>();
    EXPECT_EQ(n0, n1);
}

TEST_F(test_graph, test_clear_cache) {
    CreateCountNode::nCreate = 0;
    {
        Graph g;

        auto n0 = g.add<CreateCountNode>();
        EXPECT_EQ(CreateCountNode::nCreate, 1);

        auto n1 = g.add<CreateCountNode>();
        EXPECT_EQ(CreateCountNode::nCreate, 1);

        EXPECT_EQ(n0, n1);
    }
    {
        EXPECT_EQ(CreateCountNode::nCreate, 1);
        Graph g;

        auto n0 = g.add<CreateCountNode>();
        EXPECT_EQ(CreateCountNode::nCreate, 2);

        auto n1 = g.add<CreateCountNode>();
        EXPECT_EQ(CreateCountNode::nCreate, 2);

        EXPECT_EQ(n0, n1);
    }
}

TEST_F(test_graph, test_dedup_str_char) {
    const char* symbolChar = "NASDAQ:AAPL";
    std::string symbolStr(symbolChar);

    auto nodeChar = g->add<RawMarketData>(symbolChar);
    auto nodeStr = g->add<RawMarketData>(symbolStr);
    EXPECT_EQ(nodeChar, nodeStr);
}


TEST_F(test_graph, set_clock) {
    auto md = g->add<MockEventSourceMarketData>("NASDAQ:TSLA");
    auto mockNode =  new NiceMock<MockValueNode>(g);
    
    // mockNode will get a callback from md.
    mockNode->setClock(md);

    // ok, hijack the market data.
    md::Book b;
    MockBookFiniteDepthMsg msg;
    msg.setOutrightBook(&b);

    // create some market data.

    b.insert({123, Side::Bid, 1, 100.0});
    b.insert({124, Side::Ask, 1, 101.0});

    ASSERT_FALSE((mockNode->ticked()));

    md->fireBookChange(msg);

    ASSERT_TRUE((mockNode->ticked()));
    delete mockNode;
}


TEST_F(test_graph, factory_dtor) {
    {
        TestGraph tg("NASDAQ:TSLA", 1);
        auto midpt = tg.g->add<Midpt>(tg.g->add<RawMarketData>("NASDAQ:TSLA"));
        ASSERT_TRUE((midpt));
        // midpt should have created a MarketData node.
        auto md = midpt->market_data_;
        ASSERT_TRUE((md));
        //ASSERT_EQ(midpt->getGraph(), md->getGraph());

        ASSERT_EQ(midpt->numChildren(), 0u);
        ASSERT_EQ(midpt->numParents(), 0u); 
        ASSERT_EQ(midpt->numClocks(), 1u);
        ASSERT_EQ(midpt->numCallbacks(), 0u);
    }
    // Graph dtor should have been called.
    {
        TestGraph tg("NASDAQ:TSLA", 1);
        // Try to get RawMarketData("NASDAQ:TSLA") again, should have no
        // children.
        auto md = tg.g->add<RawMarketData>("NASDAQ:TSLA");
        ASSERT_TRUE((md));
        //EXPECT_EQ(md->numCallbacks(), 0u);
    }
}


TEST_F(test_graph, multiple_graphs) {
    Graph* g1 = strategy.newGraph();
    Graph* g2 = strategy.newGraph();
    
    auto sig1 = g1->add<Midpt>(g1->add<RawMarketData>("NASDAQ:TSLA"));
    auto sig2 = g2->add<Midpt>(g2->add<RawMarketData>("NASDAQ:TSLA"));
    ASSERT_NE(sig1, sig2);
    ASSERT_NE(sig1->market_data_, sig2->market_data_);
}


TEST_F(test_graph, tree_updated) {
    MockSourceNode src(g, "NASDAQ:TSLA");
    MockValueNode sig1(g), sig2(g), sig3(g);

    EXPECT_EQ(src.computeOrder_.size(), 0u);

    sig1.setClock(&src);
    ASSERT_EQ(src.computeOrder_.size(), 1u);
    EXPECT_EQ(src.computeOrder_[0], &sig1);

    sig2.setClock(&sig1);
    ASSERT_EQ(src.computeOrder_.size(), 2u);
    EXPECT_EQ(src.computeOrder_[1], &sig2);

    sig3.setClock(&sig1);
    ASSERT_EQ(src.computeOrder_.size(), 3u);
    std::vector<Node*> last_two(src.computeOrder_.end() - 2,
                                src.computeOrder_.end());
    EXPECT_THAT(last_two, UnorderedElementsAre(&sig2, &sig3));
}

// Make sure signals are only fired once when two clocks are set.
TEST_F(test_graph, test_no_duplicate_fire) {
    MockSourceNode src(g, "NASDAQ:TSLA");
    MockValueNode sig1(g), sig2(g), val(g);

    sig1.setClock(&src);
    sig2.setClock(&src);
    
    // val depends on both sig1 and sig2
    // and fires whenever either does.
    val.setClock(&sig1, &sig2);

    EXPECT_CALL(sig1, compute())
        .Times(1);
    ON_CALL(sig1, compute())
        .WillByDefault(Invoke(&sig1, &MockValueNode::setValid));


    EXPECT_CALL(sig2, compute())
        .Times(1);
    ON_CALL(sig2, compute())
        .WillByDefault(Invoke(&sig2, &MockValueNode::setValid));

    EXPECT_CALL(val, compute())
        .Times(1);

    ASSERT_FALSE((sig1.ticked()));
    ASSERT_FALSE((sig2.ticked()));
    ASSERT_FALSE((val.ticked()));

    src.fire();

    ASSERT_TRUE((sig1.ticked()));
    ASSERT_TRUE((sig2.ticked()));
    ASSERT_TRUE((val.ticked()));
}

TEST_F(test_graph, compute_pruning) {
    // Tests that children are not called when parent compute fails.
    MockSourceNode src(g, "NASDAQ:TSLA");
    MockValueNode sig1(g), sig2(g), sig3(g), sig4(g);

    sig1.setClock(&src);

    sig2.setParent(&sig1);
    sig2.setClock(&src);

    sig3.setParent(&sig2);
    sig3.setClock(&src);

    sig4.setClock(&src);

    // sig1 will be invalid... 
    ON_CALL(sig1, compute())
        .WillByDefault(Invoke(&sig1, &MockValueNode::setInvalid));

    EXPECT_CALL(sig1, compute())
        .Times(1);
    //these two should not have parentsValid(), so shouldn't compute
    EXPECT_CALL(sig2, compute())
        .Times(0);
    EXPECT_CALL(sig3, compute())
        .Times(0);
    // depends only on src so should be called once.
    EXPECT_CALL(sig4, compute())
        .Times(1);

    ASSERT_FALSE((sig1.ticked()));

    src.fire();

    ASSERT_TRUE((src.valid()));
    ASSERT_FALSE((sig1.valid()));
    ASSERT_FALSE((sig2.valid()));
    ASSERT_FALSE((sig3.valid()));
    ASSERT_FALSE((sig2.parentsValid()));
    ASSERT_FALSE((sig3.parentsValid()));
    ASSERT_TRUE((sig4.parentsValid()));

    //ticked just means their clock fired.
    ASSERT_TRUE((src.ticked()));
    ASSERT_TRUE((sig1.ticked()));
    ASSERT_TRUE((sig2.ticked()));
    ASSERT_TRUE((sig3.ticked()));
    ASSERT_TRUE((sig4.ticked()));

}

TEST_F(test_graph, test_topological_sort) {
    MockInitNode src0(g), src1(g), w0(g), m0(g), m1(g), ct0(g), val(g);
    w0.setParent(&src0);
    m0.setParent(&src0);
    m1.setParent(&src1);
    ct0.setParent(&w0);
    ct0.setParent(&m1);
    val.setParent(&m0);
    val.setParent(&ct0);

    //           src0     src1
    //          /   \      |
    //         m0    w0    m1
    //         |      \   /
    //          \      ct0
    //           \     /
    //            \   /
    //             val
    
    //Note the ordering of a valid topological sort is implementation dependent
    std::vector<Node*> order;
    topological_sort(&src0, order);

    ASSERT_EQ(order.size(), 5u);
    
    ASSERT_TRUE(br::find(order,&src1)==order.end());
    ASSERT_TRUE(br::find(order,&m1)==order.end());
    ASSERT_TRUE(br::find(order,&val)!=order.end());

    ASSERT_TRUE(br::find(order,&m0)>br::find(order,&src0));
    ASSERT_TRUE(br::find(order,&w0)>br::find(order,&src0));
    ASSERT_TRUE(br::find(order,&ct0)>br::find(order,&w0));
    ASSERT_TRUE(br::find(order,&val)>br::find(order,&ct0));
    ASSERT_TRUE(br::find(order,&val)>br::find(order,&m0));

    order.clear();
    topological_sort(&src1, order);

    ASSERT_EQ(order.size(), 4u);
    ASSERT_TRUE(br::find(order,&src0)==order.end());
    ASSERT_TRUE(br::find(order,&m0)==order.end());
    ASSERT_TRUE(br::find(order,&w0)==order.end());
    ASSERT_TRUE(br::find(order,&val)!=order.end());

    ASSERT_TRUE(br::find(order,&m1)>br::find(order,&src1));
    ASSERT_TRUE(br::find(order,&ct0)>br::find(order,&m1));
    ASSERT_TRUE(br::find(order,&val)>br::find(order,&ct0));
}


TEST_F(test_graph, theo_factory) {
    std::vector<std::string> v { "WeightAve", "Midpt", "CompTheo", "EMA" };
    for ( auto& type : v ) {
        EXPECT_NO_THROW(Graph::find_type(type));
    }
}
