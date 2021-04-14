#include "model/protection_adjusters.h"
#include "model/order_logic.h"

NODE_FACTORY_ADD(BadMarkups);
NODE_FACTORY_ADD(BadMarkupCount);
NODE_FACTORY_ADD(FastMarket);
NODE_FACTORY_ADD(IOCAlreadySent);
NODE_FACTORY_ADD(LowLiquidity);
NODE_FACTORY_ADD(RecentFill);
NODE_FACTORY_ADD(SafeUpdateFailed);
NODE_FACTORY_ADD(ThruBook);
NODE_FACTORY_ADD(TimeThruBook);
NODE_FACTORY_ADD(WideSpread);

BadMarkups::BadMarkups(Graph* g, std::string order_logic_name, std::chrono::nanoseconds markup_horizon, 
                       double decay_pct, double threshold, int buffer_size)
    : ValueNode(g),
      order_logic_name_(order_logic_name),
      markup_horizon_(markup_horizon), 
      decay_pct_(decay_pct),
      threshold_(threshold),
      buffer_size_(buffer_size),
      stored_values_(buffer_size) {
    value_ = false;
    auto strategy = g->getStrategy();
    assert(strategy);
    private_msg_ = g->add<MsgAck>(order_logic_name);
    assert(private_msg_);

    auto orderLogic = strategy->getOrderLogic(order_logic_name);
    assert(orderLogic);
    const auto& symbol = orderLogic->symbol().name();
    auto market_data = g->add<RawMarketData>(symbol);
    midpt_ = g->add<Midpt>(market_data);
    setParent(midpt_);
    setClock(private_msg_, g->add<OnQuote>(market_data));
}

BadMarkupCount::BadMarkupCount(Graph* g, std::string order_logic_name, std::chrono::nanoseconds markup_horizon, 
                       double decay_pct, double threshold, int buffer_size)
    : ValueNode(g),
      order_logic_name_(order_logic_name),
      markup_horizon_(markup_horizon), 
      decay_pct_(decay_pct),
      threshold_(threshold),
      buffer_size_(buffer_size) {
    value_ = 0;
    bad_markups_ = g->add<BadMarkups>(order_logic_name, markup_horizon, decay_pct, threshold, buffer_size);
    setParent(bad_markups_);
    setClock(bad_markups_);
}


//TODO(mshivers): add a MsgThrottle adjuster that prevents sending another order at the same price for 10ms
//TODO(mshivers): update IOCAlreadySent to prevent IOCs at the same price/side as the last IOC until the 
//valuation goes back through the price.
IOCAlreadySent::IOCAlreadySent(Graph* g, std::string order_logic_name)
    : ValueNode(g),
      order_logic_name_(order_logic_name) {
    value_ = false;

    auto strategy = g->getStrategy();
    assert(strategy);

    OrderLogic* order_logic = strategy->getOrderLogic(order_logic_name);
    assert(order_logic);
    valuation_ = order_logic->valuation();
    const auto& symbol = order_logic->symbol().name();
    md_ = g->add<RawMarketData>(symbol);
    assert(md_);
    send_msg_ = g->add<SendMsg>(order_logic_name);
    assert(send_msg_);

    setParents(md_, send_msg_, valuation_);
    setClock(send_msg_, valuation_); 
} 


RecentFill::RecentFill(Graph* g, std::string order_logic_name, Side no_order_side, 
               std::chrono::nanoseconds wait_duration)
    : ValueNode(g), 
      order_logic_name_(order_logic_name),
      no_order_side_(no_order_side),
      wait_duration_(wait_duration) {
    
    value_ = false;
    private_msg_ = g->add<MsgAck>(order_logic_name);
    assert(private_msg_);

    auto strategy = g->getStrategy();
    assert(strategy);
    auto order_logic = strategy->getOrderLogic(order_logic_name);
    assert(order_logic);
    const auto& symbol = order_logic->symbol().name();
    setClock(private_msg_, g->add<OnUpdate>(g->add<RawMarketData>(symbol)));
}         

    

