#ifndef TCP_COPA_H
#define TCP_COPA_H

#include "ns3/rtt-estimator.h"
#include "ns3/tcp-congestion-ops.h"

namespace ns3 {

// Implements Kathleen Nichols' algorithm for tracking the minimum (or maximum)
// estimate of a stream of samples over some fixed time interval.
template <class T> struct MinFilter {
  bool operator()(const T &lhs, const T &rhs) const { return lhs <= rhs; }
};

template <class T> struct MaxFilter {
  bool operator()(const T &lhs, const T &rhs) const { return lhs >= rhs; }
};

template <class T, class Compare> class WindowedFilter {
public:
  WindowedFilter(Time windowLength) : windowLength(windowLength) {}

  // Changes the window length. Does not update any current samples.
  void SetWindowLength(Time windowLength) { this->windowLength = windowLength; }

  // Updates best estimates with |sample|, and expires and updates best
  // estimates as necessary.
  void Update(T newSample, Time newTimestamp) {
    // Reset all estimates if they have not yet been initialized, if new sample
    // is a new best, or if the newest recorded estimate is too old.
    if (!initialized || Compare()(newSample, estimates[0].sample) ||
        newTimestamp - estimates[2].timestamp > windowLength) {
      Reset(newSample, newTimestamp);
      initialized = true;
      return;
    }

    if (Compare()(newSample, estimates[1].sample)) {
      estimates[1] = Sample(newSample, newTimestamp);
      estimates[2] = estimates[1];
    } else if (Compare()(newSample, estimates[2].sample)) {
      estimates[2] = Sample(newSample, newTimestamp);
    }

    // Expire and update estimates as necessary.
    if (newTimestamp - estimates[0].timestamp > windowLength) {
      // The best estimate hasn't been updated for an entire window, so promote
      // second and third best estimates.
      estimates[0] = estimates[1];
      estimates[1] = estimates[2];
      estimates[2] = Sample(newSample, newTimestamp);
      // Need to iterate one more time. Check if the new best estimate is
      // outside the window as well, since it may also have been recorded a
      // long time ago. Don't need to iterate once more since we cover that
      // case at the beginning of the method.
      if (newTimestamp - estimates[0].timestamp > windowLength) {
        estimates[0] = estimates[1];
        estimates[1] = estimates[2];
      }
      return;
    }
    if (estimates[1].sample == estimates[0].sample &&
        newTimestamp - estimates[1].timestamp > windowLength / 4) {
      // A quarter of the window has passed without a better sample, so the
      // second-best estimate is taken from the second quarter of the window.
      estimates[2] = estimates[1] = Sample(newSample, newTimestamp);
      return;
    }

    if (estimates[2].sample == estimates[1].sample &&
        newTimestamp - estimates[2].timestamp > windowLength / 2) {
      // We've passed a half of the window without a better estimate, so take
      // a third-best estimate from the second half of the window.
      estimates[2] = Sample(newSample, newTimestamp);
    }
  }

  void Update(T newSample) { Update(newSample, Simulator::Now()); }

  void Reset(T newSample, Time newTimestamp) {
    estimates[0] = estimates[1] = estimates[2] =
        Sample(newSample, newTimestamp);
    initialized = true;
  }

  T GetBest() const { return estimates[0].sample; }
  T GetSecondBest() const { return estimates[1].sample; }
  T GetThirdBest() const { return estimates[2].sample; }

private:
  struct Sample {
    T sample;
    Time timestamp;
    Sample(T sample, Time timestamp) : sample(sample), timestamp(timestamp) {}
    Sample() {}
  };

  bool initialized = false;
  Time windowLength;
  Sample estimates[3];
};

class TcpCopa : public TcpCongestionOps {
public:
  static TypeId GetTypeId(void);

  TcpCopa(void);

  TcpCopa(const TcpCopa &sock);

  virtual ~TcpCopa(void);

  virtual std::string GetName(void) const override;

  virtual uint32_t GetSsThresh(Ptr<const TcpSocketState> tcb,
                               uint32_t bytesInFlight) override;

  virtual void IncreaseWindow(Ptr<TcpSocketState> tcb,
                              uint32_t segmentsAcked) override;

  virtual void PktsAcked(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked,
                         const Time &rtt) override;

  virtual void
  CongestionStateSet(Ptr<TcpSocketState> tcb,
                     const TcpSocketState::TcpCongState_t newState) override;

  virtual Ptr<TcpCongestionOps> Fork() override;

private:
  // The velocity parameter, v, speeds-up convergence.
  struct Velocity {
    enum class Direction { None, Up, Down };
    // It is initialized to 1.
    uint32_t value = 1;

    Direction direction = Direction::None;

    uint64_t numDirectionRemainedSame = 0;

    uint32_t lastCwnd = UINT32_MAX;
    Time lastCwndTimestamp;
  };

  WindowedFilter<Time, MinFilter<Time>> minRttFilter;
  WindowedFilter<Time, MinFilter<Time>> standingRttFilter;
  RttMeanDeviation srttEstimator;

  double delta = 0.5;
  Velocity velocity;

  bool isSlowStart = true;
  Time lastCwndDoubleTimestamp;
  bool lastCwndDoubleTimestampHasValue = false;

  void CheckAndUpdateDirection(Ptr<TcpSocketState> tcb);

  void ChangeDirection(Ptr<TcpSocketState> tcb,
                       Velocity::Direction newDirection);
};

} // namespace ns3

#endif /* TCP_COPA_H */
