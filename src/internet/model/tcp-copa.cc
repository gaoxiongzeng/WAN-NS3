#include "tcp-copa.h"

#include <ns3/abort.h>
#include <ns3/log.h>
#include <ns3/node.h>
#include <ns3/simulator.h>
#include <ns3/trace-source-accessor.h>

NS_LOG_COMPONENT_DEFINE("TcpCopa");

namespace ns3 {
NS_OBJECT_ENSURE_REGISTERED(TcpCopa);

TypeId TcpCopa::GetTypeId(void) {
  static TypeId tid = TypeId("ns3::TcpCopa")
                          .SetParent<TcpCongestionOps>()
                          .SetGroupName("Internet")
                          .AddConstructor<TcpCopa>();
  return tid;
}

TcpCopa::TcpCopa(void)
    : TcpCongestionOps(), minRttFilter(Seconds(10)),
      standingRttFilter(MilliSeconds(100)) {
  NS_LOG_FUNCTION(this);
}

TcpCopa::TcpCopa(const TcpCopa &sock)
    : TcpCongestionOps(sock), minRttFilter(Seconds(10)),
      standingRttFilter(MilliSeconds(100)) {
  NS_LOG_FUNCTION(this);
}

TcpCopa::~TcpCopa(void) {}

std::string TcpCopa::GetName() const {
  NS_LOG_FUNCTION(this);
  return "TcpCopa";
}

// Copa does not use ssthresh.
uint32_t TcpCopa::GetSsThresh(Ptr<const TcpSocketState> tcb,
                              uint32_t bytesInFlight) {
  NS_LOG_FUNCTION(this << tcb << bytesInFlight);
  return tcb->m_cWnd;
}

// Copa ignores calls to increase window.
void TcpCopa::IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked) {
  NS_LOG_FUNCTION(this << tcb << segmentsAcked);
  return;
}

void TcpCopa::PktsAcked(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked,
                        const Time &rtt) {
  // subtractAndCheckUnderflow(conn_.lossState.inflightBytes, ack.ackedBytes);

  auto lrtt = rtt; // Last rtt

  minRttFilter.SetWindowLength(lrtt.Max());
  minRttFilter.Update(lrtt);
  auto rttMin = minRttFilter.GetBest();

  // 1. Update the queuing delay dq using Eq. (2) and srtt
  //    using the standard TCP exponentially weighted
  //    moving average estimator.
  srttEstimator.Measurement(lrtt);
  auto srtt = srttEstimator.GetEstimate();

  if (srtt.IsNegative()) {
    // seems like a bug in srttEstimator?
    NS_LOG_DEBUG(this << " lrtt: " << lrtt << " srtt is negative: " << srtt);
    srttEstimator.Reset(); 
    return;
  }

  standingRttFilter.SetWindowLength(srtt / 2);
  standingRttFilter.Update(lrtt);
  auto rttStanding = standingRttFilter.GetBest();

  NS_LOG_INFO(this << " lrtt: " << lrtt << " srtt: " << srtt 
                                        << " rttMin: " << rttMin
                                        << " rttStanding: " << rttStanding);

  Time delay = rttStanding - rttMin;

  bool increaseCwnd = false;

  if (delay.IsZero()) {
    increaseCwnd = true;
  } else {
    // 2. Set λt =1/(δ ·dq) according to Eq. (1).
    auto targetRate = (1.0 * tcb->m_segmentSize * 1000000) /
                      (delta * delay.GetMicroSeconds());
    auto currentRate =
        (1.0 * tcb->m_cWnd * 1000000) / rttStanding.GetMicroSeconds();
    increaseCwnd = targetRate >= currentRate;
    NS_LOG_INFO("increaseCwnd=" << (increaseCwnd ? "true" : "false")
                                 << " targetRate=" << targetRate
                                 << " currentRate=" << currentRate);
  }

  if (!(increaseCwnd && isSlowStart)) {
    // change direction
    CheckAndUpdateDirection(tcb);
  }

  if (increaseCwnd) {
    if (isSlowStart)
      // When a flow starts, Copa performs slow-start where
      // cwnd doubles once per RTT until current rate exceeds target rate.
      tcb->m_cWnd += segmentsAcked * tcb->m_segmentSize;

    else {
      if (velocity.direction != Velocity::Direction::Up &&
          velocity.value > 1.0) {
        ChangeDirection(tcb, Velocity::Direction::Up);
      }
      uint32_t addition = (segmentsAcked * tcb->m_segmentSize *
                           tcb->m_segmentSize * velocity.value) /
                          (delta * tcb->m_cWnd);
      tcb->m_cWnd += addition;
    }
  } else {
    if (velocity.direction != Velocity::Direction::Down &&
        velocity.value > 1.0) {
      ChangeDirection(tcb, Velocity::Direction::Down);
    }
    uint32_t reduction = (segmentsAcked * tcb->m_segmentSize *
                          tcb->m_segmentSize * velocity.value) /
                         (delta * tcb->m_cWnd);
    if (tcb->m_initialCWnd + reduction > tcb->m_cWnd) {
      tcb->m_cWnd = tcb->m_initialCWnd;
    } else {
      tcb->m_cWnd -= reduction;
    }
    isSlowStart = false;
    NS_LOG_INFO(Simulator::Now() << " isSlowStart goes to false");
  }
}

void TcpCopa::CheckAndUpdateDirection(Ptr<TcpSocketState> tcb) {
  if (velocity.lastCwnd == UINT32_MAX) {
    velocity.lastCwnd = tcb->m_cWnd;
    velocity.lastCwndTimestamp = Simulator::Now();
    return;
  }

  auto srtt = srttEstimator.GetEstimate();

  if (srtt.IsNegative()) {
    NS_LOG_DEBUG(this << " CheckDirection - srtt is negative: " << srtt);
    return;
  }

  auto elapsedTime = Simulator::Now() - velocity.lastCwndTimestamp;

  if (elapsedTime < srtt)
    return;

  auto newDirection = tcb->m_cWnd > velocity.lastCwnd
                          ? Velocity::Direction::Up
                          : Velocity::Direction::Down;

  bool optimizedVelocity = true;

  if (newDirection == velocity.direction) {
    // if direction is the same as in the previous window, then double v.
    velocity.numDirectionRemainedSame += 1;
    uint64_t velocityDirectionThreshold = 3;
    // However, start doubling v only after the direction
    // has remained the same for three RTTs.
    if (velocity.numDirectionRemainedSame > velocityDirectionThreshold) {
      velocity.value *= 2;
      if (optimizedVelocity)
        velocity.numDirectionRemainedSame = 0;
    }

  } else {
    // If not, then reduce v back to 1.
    if (optimizedVelocity && velocity.value > 1.0)
      velocity.value /= 2;
    else
      velocity.value = 1.0;
    
    velocity.numDirectionRemainedSame = 0;
  }

  velocity.direction = newDirection;
  velocity.lastCwnd = tcb->m_cWnd;
  velocity.lastCwndTimestamp = Simulator::Now();
}

void TcpCopa::ChangeDirection(Ptr<TcpSocketState> tcb,
                              Velocity::Direction newDirection) {
  if (velocity.direction == newDirection) {
    return;
  }
  velocity.direction = newDirection;
  velocity.value = 1.0;
  velocity.numDirectionRemainedSame = 0;
  velocity.lastCwnd = tcb->m_cWnd;
  velocity.lastCwndTimestamp = Simulator::Now();
}

void TcpCopa::CongestionStateSet(
    Ptr<TcpSocketState> tcb, const TcpSocketState::TcpCongState_t newState) {
  // Copa in default mode does not use loss as a
  // congestion signal and lost packets only impact Copa to
  // the extent that they occupy wasted transmission slots in
  // the congestion window.
  return;
}

Ptr<TcpCongestionOps> TcpCopa::Fork(void) { return CopyObject<TcpCopa>(this); }

} // namespace ns3
