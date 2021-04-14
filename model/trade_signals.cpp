
#include "trade_signals.h"

NODE_FACTORY_ADD(CorrSV);
NODE_FACTORY_ADD(EMSSigmoidSV);
NODE_FACTORY_ADD(PersistentSV);
NODE_FACTORY_ADD(ProdSV);
NODE_FACTORY_ADD(SigmoidSV);
NODE_FACTORY_ADD(TreeSV);

//fast approximation to a sigmoid (should be about 4 times faster, according to stackoverflow
double approxSigmoid(double x, int C) {
    return x / (C + std::abs(x));
}

double shrinkToZero(double a, double b) {
    return std::max(std::min(a,b), std::min(std::max(a,b),0.0)); //median
}

std::string SignedVolume::symbol() const { return market_data_->symbol(); }
std::string SignedVolume::shortSymbol() const { return getShortSymbol(symbol()); }
MarketData* SignedVolume::marketData() { return market_data_; }

SignedVolume::SignedVolume(Graph* g, std::string symbol)
    : ValueNode(g, Units::NONE)
    , market_data_(g->add<RawMarketData>(symbol))
{}
SignedVolume::SignedVolume(Graph* g, MarketData* market_data)
    : ValueNode(g, Units::NONE),
      market_data_(market_data)
{}
