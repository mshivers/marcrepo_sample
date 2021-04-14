#include "mock_node.h"

#include <gtest/gtest.h>

#include "model/clocks.h"
#include "model/graph.h"
#include "model/theos.h"
#include "model/test/utils.h"

#include <functional>

// tests for node.h stuff.


struct test_node : public ::testing::Test, TestGraph {
    test_node() : TestGraph("NYSE:IBM", 1)
    {}
};

TEST_F(test_node, test_setClock) {
    MockInitNode sig1(g), sig2(g);
    auto midpt = g->add<Midpt>(g->add<RawMarketData>("NYSE:IBM"));
    MockValueNode val(g);

    ClockSet combined = combineClocks(&sig1, &sig2);
    ASSERT_EQ(combined.size(), 1u);
    ASSERT_TRUE(hasSameClock(&sig1, &sig2));

    val.setClock(&sig1, &sig2);
    ASSERT_TRUE(hasSameClock(&val, &sig1));

    std::vector<Node*> nodeVec{&sig2, midpt};
    NodeSet nodeSet{&sig2, midpt};

    MockValueNode val1(g), val2(g), val3(g);
    val1.setClock(&sig2, midpt);
    val2.setClock(nodeVec);
    val3.setClock(nodeSet);
    ASSERT_TRUE(val1.getClock() == val2.getClock());
    ASSERT_TRUE(val2.getClock() == val3.getClock());
}

TEST_F(test_node, test_combineNodes) {
    MockInitNode sig1(g), sig2(g), sig3(g);
    MockValueNode val(g);
    val.setClock(&sig1, &sig2);
    
    std::vector<Node*> nodeVec{&sig1, &sig2};
    NodeSet nodeSet{&sig2, &sig3};
    
    //Test with one single
    NodeSet combined0 = combineNodes(&sig1);
    ASSERT_TRUE((combined0.size()==1));
    ASSERT_TRUE((*combined0.begin()==&sig1));
  
    //Test with two singles
    NodeSet combined1 = combineNodes(&sig1, &sig2);
    ASSERT_TRUE((combined1.size()==2));

    //Test with one vector
    NodeSet combined2 = combineNodes(nodeVec);
    ASSERT_TRUE((combined2.size()==2));

    //Test with vector and single
    NodeSet combined3 = combineNodes(&val, nodeVec);
    ASSERT_TRUE((combined3.size()==3));
  
    //Test with 2 containers
    NodeSet combined4 = combineNodes(nodeSet, nodeVec);
    ASSERT_TRUE((combined4.size()==3));

    //Test with one set and a single
    NodeSet combined5 = combineNodes(nodeSet, &val);
    ASSERT_TRUE((combined5.size()==3));
}

TEST_F(test_node, test_setClock_sets_parent__ValueNode_ValueNodeArg) {
    MockSourceNode src(g, "dummy");

    MockValueNode child(g);
    child.setClock(&src);

    MockValueNode grandChild(g);
    grandChild.setClock(&child);

    // child.setName("child");
    // grandChild.setName("grandChild");
    // g->saveGraphViz();
    
    EXPECT_TRUE(grandChild.hasParent(&child));
}

TEST_F(test_node, test_setClock_sets_parent__Clock_ValueNodeArg) {
    MockSourceNode src(g, "dummy");

    MockValueNode child(g);
    child.setClock(&src);

    //SourceNodes are never parents, because they're always called first.
    //EXPECT_TRUE(child.hasParent(&src));
    EXPECT_TRUE(child.hasClock(&src));

    MockClockNode grandChild(g);
    grandChild.setClock(&child);

    // child.setName("child");
    // grandChild.setName("grandChild");
    // g->saveGraphViz();
    
    EXPECT_TRUE(grandChild.hasParent(&child));
}

TEST_F(test_node, test_setClock_sets_parent__ValueNode_ClockArg) {
    MockSourceNode src(g, "dummy");

    MockClockNode child(g);
    child.setClock(&src);

    MockValueNode grandChild(g);
    grandChild.setClock(&child);

    // child.setName("child");
    // grandChild.setName("grandChild");
    // g->saveGraphViz();
    
    EXPECT_TRUE(grandChild.hasClock(&child));
}

TEST_F(test_node, test_setClock_sets_parent__Clock_ClockArg) {
    MockSourceNode src(g, "dummy");

    MockClockNode child(g);
    child.setClock(&src);

    MockClockNode grandChild(g);
    grandChild.setClock(&child);

    // child.setName("child");
    // grandChild.setName("grandChild");
    // g->saveGraphViz();
    
    EXPECT_FALSE(grandChild.hasParent(&child));  //clocks don't need to add their clock as a parent
    EXPECT_TRUE(grandChild.hasClock(&child));
}

TEST_F(test_node, test_setClock_sets_parent__three_clock_deep) {
    MockSourceNode src(g, "dummy");

    MockClockNode child(g);
    child.setClock(&src);

    MockClockNode grandChild(g);
    grandChild.setClock(&child);

    MockClockNode greatGrandChild(g);
    greatGrandChild.setClock(&grandChild);

    // child.setName("child");
    // grandChild.setName("grandChild");
    // greatGrandChild.setName("greatGrandChild");
    // g->saveGraphViz();
    
    EXPECT_FALSE(greatGrandChild.hasParent(&grandChild)); //clocks don't need to add their clock as a parent
    EXPECT_TRUE(greatGrandChild.hasClock(&grandChild));
}

TEST_F(test_node, test_setClock_sets_parent__three_deep__all_combinations) {
    using NodeMaker = std::function<Node*(Graph*, Node*)>;
    
    NodeMaker mkClock = [] (Graph* g, Node* clock) -> Node*
        {
            Node* n = new MockClockNode(g);
            n->setClock(clock);
            return n;
        };
    NodeMaker mkValue = [] (Graph* g, Node* clock) -> Node*
        {
            Node* n = new MockValueNode(g);
            n->setClock(clock);
            return n;
        };

    std::vector<NodeMaker> mkFuncs{mkClock, mkValue};

    for(auto const& mkChild : mkFuncs) {
        for(auto const& mkGrandChild : mkFuncs) {
            for(auto const& mkGreatGrandChild : mkFuncs) {
                Graph g;
                MockSourceNode src(&g, "dummy");

                auto child = mkChild(&g, &src);
                auto grandChild = mkGrandChild(&g, child);
                auto greatGrandChild = mkGreatGrandChild(&g, grandChild);

                // child.setName("child");
                // grandChild.setName("grandChild");
                // greatGrandChild.setName("greatGrandChild");
                // g->saveGraphViz();

                EXPECT_TRUE(greatGrandChild->hasClock(grandChild) ||
                            greatGrandChild->hasParent(grandChild));

                delete child;
                delete grandChild;
                delete greatGrandChild;
            }
        }
    }
}

