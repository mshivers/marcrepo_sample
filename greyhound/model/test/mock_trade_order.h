#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "model/order_logic.h"

// Mock objects to test IOrderUpdate interface.
struct MockTradeOrderMsg : public vhl::IvTradeOrderMsg
{
    MockTradeOrderMsg(size_t tradeOrderIndex,
                      vpl::UInt64 exchangeOrderId=0,
                      vpl::CPrice price=vpl::CPrice{},
                      size_t exchangeIndex=0,
                      size_t instrumentIndex=0)
        : tradeOrderIndex_(tradeOrderIndex)
        , exchangeIndex_(exchangeIndex)
        , instrumentIndex_(instrumentIndex)
        , price_(price)
        , exchangeOrderId_(exchangeOrderId)
    {}
    protected:
    size_t tradeOrderIndex_, exchangeIndex_, instrumentIndex_;
    vpl::CPrice price_;
    vpl::UInt64 exchangeOrderId_;

    public:
    size_t tradeOrderIndex() const override { return tradeOrderIndex_; }
    size_t instrumentIndex() const override { return instrumentIndex_; }
    size_t exchangeIndex() const override { return exchangeIndex_; }
    vpl::CPrice const& price() const override { return price_; }

    MOCK_CONST_METHOD1(avgPrice, vpl::CPrice(vpl::Int32));
    MOCK_CONST_METHOD0(clientOrderId, vpl::CString const&());
    MOCK_CONST_METHOD0(clientOrderIdOld, vpl::CString const&());
    MOCK_CONST_METHOD0(clientOrderIdFirst, vpl::CString const&());
    MOCK_CONST_METHOD0(sessionId, vpl::CString const&());
    MOCK_CONST_METHOD0(errText, vpl::CString const&());
    MOCK_CONST_METHOD0(createdTimestamp, vpl::CTimeSpec const&());
    MOCK_CONST_METHOD0(isOpen, bool());
    MOCK_CONST_METHOD0(isOpening, bool());
    MOCK_CONST_METHOD0(isPending, bool());
    MOCK_CONST_METHOD0(orderType, vhl::OrderTypes());
    MOCK_CONST_METHOD0(pendingState, vhl::vTradeOrderPendingStates());
    MOCK_CONST_METHOD0(pendingTradeOrderIndex, size_t());
    MOCK_CONST_METHOD0(pprice, vpl::CPrice const&());
    MOCK_CONST_METHOD0(psize, vpl::Int64());
    MOCK_CONST_METHOD0(qdone, vpl::Int64());
    MOCK_CONST_METHOD0(qopen, vpl::Int64());
    MOCK_CONST_METHOD0(size, vpl::Int64());
    MOCK_CONST_METHOD0(side, vhl::Sides());
    MOCK_CONST_METHOD0(maxShow, vpl::UInt64());
    MOCK_CONST_METHOD0(pmaxShow, vpl::UInt64());
    vpl::UInt64 exchangeOrderId() const {return exchangeOrderId_;}
    MOCK_CONST_METHOD0(state, vhl::vTradeOrderStates());
    MOCK_CONST_METHOD0(tif, vhl::Tifs());
    MOCK_CONST_METHOD0(updateState, vhl::vTradeOrderUpdateStates());
    vpl::UInt32 const& strategyId() const override {return strategyId_;}
    vpl::UInt32 strategyId_=1;

    MOCK_CONST_METHOD0(getUserData, vpl::Int64 const());
    MOCK_METHOD1(setUserData, void(vpl::Int64));
};

struct MockTradeOrderRejectMsg : public vhl::IvTradeNewOrderRejectMsg
{
    MockTradeOrderRejectMsg(vpl::CString const& clientOrderId
        , vpl::CString const& errText)
        : clientOrderId_(clientOrderId)
        , errText_(errText)
    {}
    vpl::CString clientOrderId_, errText_;
    vpl::CString const& clientOrderId() const { return clientOrderId_; }
    vpl::CString const& errText() const { return errText_; }

    MOCK_CONST_METHOD0(exchangeIndex, size_t());
    MOCK_CONST_METHOD0(instrumentIndex, size_t());
    MOCK_CONST_METHOD0(orderType, vhl::OrderTypes());
    MOCK_CONST_METHOD0(price, vpl::CPrice const&());
    MOCK_CONST_METHOD0(receiveTimestamp, vpl::CTimeSpec const&());
    MOCK_CONST_METHOD0(sessionId, vpl::CString const&());
    MOCK_CONST_METHOD0(side, vhl::Sides());
    MOCK_CONST_METHOD0(size, vpl::Int64());
    MOCK_CONST_METHOD0(tif, vhl::Tifs());
    MOCK_CONST_METHOD0(exchangeTimestamp, vpl::CTimeSpec const&());
    virtual vpl::CTimeSpec const& exchangeTimestamp2() const override {return exchangeTimestamp2_;}
    MOCK_CONST_METHOD0(isInternal, bool());
    MOCK_CONST_METHOD0(isRecovery, bool());
    MOCK_CONST_METHOD0(getExchangeSpecificData, const void*());

    vpl::CTimeSpec exchangeTimestamp2_;
};


struct MockTradeOrderAcceptMsg : public vhl::IvTradeNewOrderAcceptMsg
{
    MockTradeOrderAcceptMsg(vpl::CString const& clientOrderId
                            , vpl::UInt64 exchangeOrderId
                            , vpl::CPrice price = vpl::CPrice()) 
        : clientOrderId_(clientOrderId)
        , exchangeOrderId_(exchangeOrderId)
        , price_(price)
    {}
    vpl::CString clientOrderId_;
    vpl::UInt64 exchangeOrderId_;
    vpl::CPrice price_;
    vpl::CString const& clientOrderId() const { return clientOrderId_; }
    vpl::UInt64 exchangeOrderId() const { return exchangeOrderId_; }

    MOCK_CONST_METHOD0(exchangeIndex, size_t());
    MOCK_CONST_METHOD0(instrumentIndex, size_t());
    MOCK_CONST_METHOD0(orderType, vhl::OrderTypes());
    vpl::CPrice const& price() const override {return price_;}
    MOCK_CONST_METHOD0(receiveTimestamp, vpl::CTimeSpec const&());
    MOCK_CONST_METHOD0(sessionId, vpl::CString const&());
    MOCK_CONST_METHOD0(side, vhl::Sides());
    MOCK_CONST_METHOD0(size, vpl::Int64());
    MOCK_CONST_METHOD0(exchangeTimestamp, vpl::CTimeSpec const&());
    virtual vpl::CTimeSpec const& exchangeTimestamp2() const override {return exchangeTimestamp2_;}
    MOCK_CONST_METHOD0(ackedDisplayType, vhl::AckedDisplayType());
    MOCK_CONST_METHOD0(isRecovery, bool());
    MOCK_CONST_METHOD0(lastMktId, size_t());
    MOCK_CONST_METHOD0(getExchangeSpecificData, const void*());
    //MOCK_CONST_METHOD0(ackedDisplayType, vhl::AckedDisplayType());

    vpl::CTimeSpec exchangeTimestamp2_;
};


struct MockTradeCancelOrderAcceptMsg : public vhl::IvTradeCancelOrderAcceptMsg 
{
    MockTradeCancelOrderAcceptMsg(vpl::CString const& clientOrderId, bool isOut=true)
        : clientOrderId_(clientOrderId)
        , isOut_(isOut)
    {}
    vpl::CString clientOrderId_;
    bool isOut_;
    vpl::CTimeSpec exchangeTimestamp2_;

    vpl::CString const& clientOrderId() const { return clientOrderId_; }
    bool isOut() const { return isOut_; }

    MOCK_CONST_METHOD0(exchangeIndex, size_t());
    MOCK_CONST_METHOD0(instrumentIndex, size_t());
    MOCK_CONST_METHOD0(receiveTimestamp, vpl::CTimeSpec const&());
    MOCK_CONST_METHOD0(exchangeTimestamp, vpl::CTimeSpec const&());
    virtual vpl::CTimeSpec const& exchangeTimestamp2() const override {return exchangeTimestamp2_;}
    MOCK_CONST_METHOD0(isRecovery, bool());
    MOCK_CONST_METHOD0(getExchangeSpecificData, const void*());
};


struct MockTradeCancelOrderRejectMsg : public vhl::IvTradeCancelOrderRejectMsg 
{
    MockTradeCancelOrderRejectMsg(vpl::CString const& clientOrderId, vpl::CString const& errText)
        : clientOrderId_(clientOrderId)
        , errText_(errText)
    {}
    vpl::CString clientOrderId_;
    vpl::CString errText_;
    vpl::CTimeSpec exchangeTimestamp2_;

    vpl::CString const& clientOrderId() const { return clientOrderId_; }
    vpl::CString const& errText() const { return errText_; }

    MOCK_CONST_METHOD0(exchangeIndex, size_t());
    MOCK_CONST_METHOD0(instrumentIndex, size_t());
    MOCK_CONST_METHOD0(receiveTimestamp, vpl::CTimeSpec const&());
    MOCK_CONST_METHOD0(exchangeTimestamp, vpl::CTimeSpec const&());
    virtual vpl::CTimeSpec const& exchangeTimestamp2() const override {return exchangeTimestamp2_;}
    MOCK_CONST_METHOD0(isRecovery, bool());
    MOCK_CONST_METHOD0(isInternal, bool());
    MOCK_CONST_METHOD0(getExchangeSpecificData, const void*());
};


struct MockTradeFillFullMsg : public vhl::IvTradeFillFullMsg
{
    MockTradeFillFullMsg(vpl::CString const& clientOrderId, uint64_t exchangeOrderId, vpl::CPrice const& price, int64_t size
            , vhl::Sides side = vhl::Sides::eSide_Undefined)
        : clientOrderId_(clientOrderId)
        , exchangeOrderId_(exchangeOrderId)
        , execPrice_(price)
        , execSize_(size)
        , side_(side)
    {}
    vpl::CString clientOrderId_;
    vpl::UInt64 exchangeOrderId_;
    vpl::CPrice execPrice_;
    vpl::Int64 execSize_;
    vpl::CTimeSpec exchangeTimestamp2_;
    vhl::Sides  side_;

    vpl::CString const& clientOrderId() const { return clientOrderId_; }
    vpl::UInt64 exchangeOrderId() const { return exchangeOrderId_; }
    vpl::CPrice  const& execPrice() const { return execPrice_; }
    vpl::Int64 execSize() const { return execSize_; }
    vhl::Sides side() const { return side_; }

    MOCK_CONST_METHOD0(execId, vpl::Int64());
    MOCK_CONST_METHOD0(exchangeIndex, size_t());
    MOCK_CONST_METHOD0(instrumentIndex, size_t());
    MOCK_CONST_METHOD0(receiveTimestamp, vpl::CTimeSpec const&());
    MOCK_CONST_METHOD0(exchangeTimestamp, vpl::CTimeSpec const&());
    virtual vpl::CTimeSpec const& exchangeTimestamp2() const override {return exchangeTimestamp2_;}
    MOCK_CONST_METHOD0(account, vpl::CString const&());
    MOCK_CONST_METHOD0(execBroker, vpl::CString const&());
    MOCK_CONST_METHOD0(sessionId, vpl::CString const&());
    MOCK_CONST_METHOD0(secondaryExecId, vpl::CString const&());
    MOCK_CONST_METHOD0(tradeLiquidityIndicator, vpl::CString const&());
    MOCK_CONST_METHOD0(feeCode, vpl::CString const&());
    MOCK_CONST_METHOD0(isRecovery, bool());
    MOCK_CONST_METHOD0(aggressorIndicator, vhl::AggressorIndicator());
    MOCK_CONST_METHOD0(lastMkt, char const*());
    MOCK_CONST_METHOD0(getExchangeSpecificData, const void*());

};

struct MockTradeFillPartialMsg : public vhl::IvTradeFillPartialMsg
{
    MockTradeFillPartialMsg(vpl::CString const& clientOrderId, uint64_t exchangeOrderId, vpl::CPrice const& price, int64_t size
            , vhl::Sides side = vhl::Sides::eSide_Undefined)
        : clientOrderId_(clientOrderId)
        , exchangeOrderId_(exchangeOrderId)
        , execPrice_(price)
        , execSize_(size)
        , side_(side)
    {}
    vpl::CString clientOrderId_;
    vpl::UInt64 exchangeOrderId_;
    vpl::CPrice execPrice_;
    vpl::Int64 execSize_;
    vpl::CTimeSpec exchangeTimestamp2_;
    vhl::Sides  side_;

    vpl::CString const& clientOrderId() const { return clientOrderId_; }
    vpl::UInt64 exchangeOrderId() const { return exchangeOrderId_; }
    vpl::CPrice  const& execPrice() const { return execPrice_; }
    vpl::Int64 execSize() const { return execSize_; }
    vhl::Sides side() const { return side_; }

    MOCK_CONST_METHOD0(execId, vpl::Int64());
    MOCK_CONST_METHOD0(exchangeIndex, size_t());
    MOCK_CONST_METHOD0(instrumentIndex, size_t());
    MOCK_CONST_METHOD0(receiveTimestamp, vpl::CTimeSpec const&());
    MOCK_CONST_METHOD0(exchangeTimestamp, vpl::CTimeSpec const&());
    virtual vpl::CTimeSpec const& exchangeTimestamp2() const override {return exchangeTimestamp2_;}
    MOCK_CONST_METHOD0(account, vpl::CString const&());
    MOCK_CONST_METHOD0(execBroker, vpl::CString const&());
    MOCK_CONST_METHOD0(sessionId, vpl::CString const&());
    MOCK_CONST_METHOD0(secondaryExecId, vpl::CString const&());
    MOCK_CONST_METHOD0(tradeLiquidityIndicator, vpl::CString const&());
    MOCK_CONST_METHOD0(feeCode, vpl::CString const&());
    MOCK_CONST_METHOD0(isRecovery, bool());
    MOCK_CONST_METHOD0(aggressorIndicator, vhl::AggressorIndicator());
    MOCK_CONST_METHOD0(lastMkt, char const*());
    MOCK_CONST_METHOD0(getExchangeSpecificData, const void*());
};


struct MockTradeOrderOutMsg : public vhl::IvTradeOrderOutMsg
{
    MockTradeOrderOutMsg(vpl::CString const& clientOrderId, vpl::CString const& errText)
        : clientOrderId_(clientOrderId)
        , errText_(errText)
    {}
    vpl::CString clientOrderId_;
    vpl::CString errText_;
    vpl::CTimeSpec exchangeTimestamp2_;

    vpl::CString const& clientOrderId() const { return clientOrderId_; }
    vpl::CString const& errText() const { return errText_; }

    MOCK_CONST_METHOD0(exchangeIndex, size_t());
    MOCK_CONST_METHOD0(instrumentIndex, size_t());
    MOCK_CONST_METHOD0(receiveTimestamp, vpl::CTimeSpec const&());
    MOCK_CONST_METHOD0(exchangeTimestamp, vpl::CTimeSpec const&());
    virtual vpl::CTimeSpec const& exchangeTimestamp2() const override {return exchangeTimestamp2_;}
    MOCK_CONST_METHOD0(isRecovery, bool());
    MOCK_CONST_METHOD0(isInternal, bool());
    MOCK_CONST_METHOD0(getExchangeSpecificData, const void*());
};


struct MockTradeAmendOrderAcceptMsg : public vhl::IvTradeAmendOrderAcceptMsg
{
    MockTradeAmendOrderAcceptMsg(vpl::CString const& clientOrderIdOld, vpl::CString const& clientOrderId)
        : clientOrderId_(clientOrderId), clientOrderIdOld_(clientOrderIdOld) {}
    vpl::CString clientOrderId_, clientOrderIdOld_;
    
    vpl::CString const& clientOrderId() const { return clientOrderId_; }
    vpl::CString const& clientOrderIdOld() const { return clientOrderIdOld_; }
    vpl::CTimeSpec exchangeTimestamp2_;
    
    MOCK_CONST_METHOD0(exchangeIndex, size_t ());
    MOCK_CONST_METHOD0(exchangeOrderId, vpl::UInt64 ());
    MOCK_CONST_METHOD0(exchangeTimestamp, vpl::CTimeSpec const &());
    MOCK_CONST_METHOD0(instrumentIndex, size_t ());
    MOCK_CONST_METHOD0(nwprice, vpl::CPrice const &());
    MOCK_CONST_METHOD0(nwsize, vpl::Int64 ());
    MOCK_CONST_METHOD0(nwmaxShow, vpl::UInt64 ());
    MOCK_CONST_METHOD0(prprice, vpl::CPrice const &());
    MOCK_CONST_METHOD0(receiveTimestamp, vpl::CTimeSpec const &());
    MOCK_CONST_METHOD0(ackedDisplayType, vhl::AckedDisplayType ());
    MOCK_CONST_METHOD0(isRecovery, bool ());
    MOCK_CONST_METHOD0(getExchangeSpecificData, const void*());
    vpl::CTimeSpec const& exchangeTimestamp2() const override {return exchangeTimestamp2_;}
};


struct MockTradeAmendOrderRejectMsg : public vhl::IvTradeAmendOrderRejectMsg
{
    MockTradeAmendOrderRejectMsg(vpl::CString const& clientOrderId)
        : clientOrderId_(clientOrderId)
    {}
    vpl::CString clientOrderId_;
    vpl::CString const errText_;
    vpl::CTimeSpec exchangeTimestamp2_;
    
    vpl::CString const& clientOrderId() const { return clientOrderId_; }
    MOCK_CONST_METHOD0(clientOrderIdOld, vpl::CString const &());
    vpl::CString const& errText() const { return errText_; }
    MOCK_CONST_METHOD0(exchangeIndex, size_t ());
    MOCK_CONST_METHOD0(exchangeTimestamp, vpl::CTimeSpec const &());
    MOCK_CONST_METHOD0(instrumentIndex, size_t ());
    MOCK_CONST_METHOD0(receiveTimestamp, vpl::CTimeSpec const &());
    MOCK_CONST_METHOD0(isRecovery, bool ());
    vpl::CTimeSpec const& exchangeTimestamp2() const override {return exchangeTimestamp2_;}
    MOCK_CONST_METHOD0(isInternal, bool ());
    MOCK_CONST_METHOD0(getExchangeSpecificData, const void*());
};
