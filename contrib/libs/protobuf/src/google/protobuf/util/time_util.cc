// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
// https://developers.google.com/protocol-buffers/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "google/protobuf/util/time_util.h"

#include <cstdint>
#include <cstdlib>

#include "google/protobuf/duration.pb.h"
#include "google/protobuf/timestamp.pb.h"
#include "y_absl/log/absl_check.h"
#include "y_absl/numeric/int128.h"
#include "y_absl/strings/str_cat.h"
#include "y_absl/strings/str_format.h"
#include "y_absl/strings/string_view.h"
#include "y_absl/time/clock.h"
#include "y_absl/time/time.h"

// Must go after other includes.
#include "google/protobuf/port_def.inc"

namespace google {
namespace protobuf {
namespace util {

using google::protobuf::Duration;
using google::protobuf::Timestamp;

namespace {
static constexpr arc_i32 kNanosPerSecond = 1000000000;
static constexpr arc_i32 kMicrosPerSecond = 1000000;
static constexpr arc_i32 kMillisPerSecond = 1000;
static constexpr arc_i32 kNanosPerMillisecond = 1000000;
static constexpr arc_i32 kNanosPerMicrosecond = 1000;
static constexpr arc_i32 kSecondsPerMinute =
    60;  // Note that we ignore leap seconds.
static constexpr arc_i32 kSecondsPerHour = 3600;

template <typename T>
T CreateNormalized(arc_i64 seconds, arc_i32 nanos);

template <>
Timestamp CreateNormalized(arc_i64 seconds, arc_i32 nanos) {
  Y_ABSL_DCHECK(seconds >= TimeUtil::kTimestampMinSeconds &&
              seconds <= TimeUtil::kTimestampMaxSeconds)
      << "Timestamp seconds are outside of the valid range";

  // Make sure nanos is in the range.
  if (nanos <= -kNanosPerSecond || nanos >= kNanosPerSecond) {
    seconds += nanos / kNanosPerSecond;
    nanos = nanos % kNanosPerSecond;
  }
  // For Timestamp nanos should be in the range [0, 999999999]
  if (nanos < 0) {
    seconds -= 1;
    nanos += kNanosPerSecond;
  }

  Y_ABSL_DCHECK(seconds >= TimeUtil::kTimestampMinSeconds &&
              seconds <= TimeUtil::kTimestampMaxSeconds &&
              nanos >= TimeUtil::kTimestampMinNanoseconds &&
              nanos <= TimeUtil::kTimestampMaxNanoseconds)
      << "Timestamp is outside of the valid range";
  Timestamp result;
  result.set_seconds(seconds);
  result.set_nanos(static_cast<arc_i32>(nanos));
  return result;
}

template <>
Duration CreateNormalized(arc_i64 seconds, arc_i32 nanos) {
  Y_ABSL_DCHECK(seconds >= TimeUtil::kDurationMinSeconds &&
              seconds <= TimeUtil::kDurationMaxSeconds)
      << "Duration seconds are outside of the valid range";

  // Make sure nanos is in the range.
  if (nanos <= -kNanosPerSecond || nanos >= kNanosPerSecond) {
    seconds += nanos / kNanosPerSecond;
    nanos = nanos % kNanosPerSecond;
  }
  // nanos should have the same sign as seconds.
  if (seconds < 0 && nanos > 0) {
    seconds += 1;
    nanos -= kNanosPerSecond;
  } else if (seconds > 0 && nanos < 0) {
    seconds -= 1;
    nanos += kNanosPerSecond;
  }

  Y_ABSL_DCHECK(seconds >= TimeUtil::kDurationMinSeconds &&
              seconds <= TimeUtil::kDurationMaxSeconds &&
              nanos >= TimeUtil::kDurationMinNanoseconds &&
              nanos <= TimeUtil::kDurationMaxNanoseconds)
      << "Duration is outside of the valid range";
  Duration result;
  result.set_seconds(seconds);
  result.set_nanos(static_cast<arc_i32>(nanos));
  return result;
}

// Format nanoseconds with either 3, 6, or 9 digits depending on the required
// precision to represent the exact value.
TProtoStringType FormatNanos(arc_i32 nanos) {
  if (nanos % kNanosPerMillisecond == 0) {
    return y_absl::StrFormat("%03d", nanos / kNanosPerMillisecond);
  } else if (nanos % kNanosPerMicrosecond == 0) {
    return y_absl::StrFormat("%06d", nanos / kNanosPerMicrosecond);
  } else {
    return y_absl::StrFormat("%09d", nanos);
  }
}

TProtoStringType FormatTime(arc_i64 seconds, arc_i32 nanos) {
  static constexpr y_absl::string_view kTimestampFormat = "%E4Y-%m-%dT%H:%M:%S";

  timespec spec;
  spec.tv_sec = seconds;
  // We only use y_absl::FormatTime to format the seconds part because we need
  // finer control over the precision of nanoseconds.
  spec.tv_nsec = 0;
  TProtoStringType result = y_absl::FormatTime(
      kTimestampFormat, y_absl::TimeFromTimespec(spec), y_absl::UTCTimeZone());
  // We format the nanoseconds part separately to meet the precision
  // requirement.
  if (nanos != 0) {
    y_absl::StrAppend(&result, ".", FormatNanos(nanos));
  }
  y_absl::StrAppend(&result, "Z");
  return result;
}

bool ParseTime(y_absl::string_view value, arc_i64* seconds, arc_i32* nanos) {
  y_absl::Time result;
  if (!y_absl::ParseTime(y_absl::RFC3339_full, value, &result, nullptr)) {
    return false;
  }
  timespec spec = y_absl::ToTimespec(result);
  *seconds = spec.tv_sec;
  *nanos = spec.tv_nsec;
  return true;
}

void CurrentTime(arc_i64* seconds, arc_i32* nanos) {
  y_absl::Time now = y_absl::Now();
  timespec spec = y_absl::ToTimespec(now);
  *seconds = spec.tv_sec;
  *nanos = spec.tv_nsec;
}

// Truncates the remainder part after division.
arc_i64 RoundTowardZero(arc_i64 value, arc_i64 divider) {
  arc_i64 result = value / divider;
  arc_i64 remainder = value % divider;
  // Before C++11, the sign of the remainder is implementation dependent if
  // any of the operands is negative. Here we try to enforce C++11's "rounded
  // toward zero" semantics. For example, for (-5) / 2 an implementation may
  // give -3 as the result with the remainder being 1. This function ensures
  // we always return -2 (closer to zero) regardless of the implementation.
  if (result < 0 && remainder > 0) {
    return result + 1;
  } else {
    return result;
  }
}
}  // namespace

// Actually define these static const integers. Required by C++ standard (but
// some compilers don't like it).
#ifndef _MSC_VER
constexpr arc_i64 TimeUtil::kTimestampMinSeconds;
constexpr arc_i64 TimeUtil::kTimestampMaxSeconds;
constexpr arc_i32 TimeUtil::kTimestampMinNanoseconds;
constexpr arc_i32 TimeUtil::kTimestampMaxNanoseconds;
constexpr arc_i64 TimeUtil::kDurationMaxSeconds;
constexpr arc_i64 TimeUtil::kDurationMinSeconds;
constexpr arc_i32 TimeUtil::kDurationMaxNanoseconds;
constexpr arc_i32 TimeUtil::kDurationMinNanoseconds;
#endif  // !_MSC_VER

TProtoStringType TimeUtil::ToString(const Timestamp& timestamp) {
  return FormatTime(timestamp.seconds(), timestamp.nanos());
}

bool TimeUtil::FromString(y_absl::string_view value, Timestamp* timestamp) {
  arc_i64 seconds;
  arc_i32 nanos;
  if (!ParseTime(value, &seconds, &nanos)) {
    return false;
  }
  *timestamp = CreateNormalized<Timestamp>(seconds, nanos);
  return true;
}

Timestamp TimeUtil::GetCurrentTime() {
  arc_i64 seconds;
  arc_i32 nanos;
  CurrentTime(&seconds, &nanos);
  return CreateNormalized<Timestamp>(seconds, nanos);
}

Timestamp TimeUtil::GetEpoch() { return Timestamp(); }

TProtoStringType TimeUtil::ToString(const Duration& duration) {
  TProtoStringType result;
  arc_i64 seconds = duration.seconds();
  arc_i32 nanos = duration.nanos();
  if (seconds < 0 || nanos < 0) {
    result = "-";
    seconds = -seconds;
    nanos = -nanos;
  }
  y_absl::StrAppend(&result, seconds);
  if (nanos != 0) {
    y_absl::StrAppend(&result, ".", FormatNanos(nanos));
  }
  y_absl::StrAppend(&result, "s");
  return result;
}

static arc_i64 Pow(arc_i64 x, int y) {
  arc_i64 result = 1;
  for (int i = 0; i < y; ++i) {
    result *= x;
  }
  return result;
}

bool TimeUtil::FromString(y_absl::string_view value, Duration* duration) {
  if (value.length() <= 1 || value[value.length() - 1] != 's') {
    return false;
  }
  bool negative = (value[0] == '-');
  size_t sign_length = (negative ? 1 : 0);
  // Parse the duration value as two integers rather than a float value
  // to avoid precision loss.
  TProtoStringType seconds_part, nanos_part;
  size_t pos = value.find_last_of('.');
  if (pos == TProtoStringType::npos) {
    seconds_part = TProtoStringType(
        value.substr(sign_length, value.length() - 1 - sign_length));
    nanos_part = "0";
  } else {
    seconds_part = TProtoStringType(value.substr(sign_length, pos - sign_length));
    nanos_part = TProtoStringType(value.substr(pos + 1, value.length() - pos - 2));
  }
  char* end;
  static_assert(sizeof(arc_i64) == sizeof(long long),
                "sizeof arc_i64 is not sizeof long long");
  arc_i64 seconds = std::strtoll(seconds_part.c_str(), &end, 10);
  if (end != seconds_part.c_str() + seconds_part.length()) {
    return false;
  }
  arc_i64 nanos = std::strtoll(nanos_part.c_str(), &end, 10);
  if (end != nanos_part.c_str() + nanos_part.length()) {
    return false;
  }
  nanos = nanos * Pow(10, static_cast<int>(9 - nanos_part.length()));
  if (negative) {
    // If a Duration is negative, both seconds and nanos should be negative.
    seconds = -seconds;
    nanos = -nanos;
  }
  duration->set_seconds(seconds);
  duration->set_nanos(static_cast<arc_i32>(nanos));
  return true;
}

Duration TimeUtil::NanosecondsToDuration(arc_i64 nanos) {
  return CreateNormalized<Duration>(nanos / kNanosPerSecond,
                                    nanos % kNanosPerSecond);
}

Duration TimeUtil::MicrosecondsToDuration(arc_i64 micros) {
  return CreateNormalized<Duration>(
      micros / kMicrosPerSecond,
      (micros % kMicrosPerSecond) * kNanosPerMicrosecond);
}

Duration TimeUtil::MillisecondsToDuration(arc_i64 millis) {
  return CreateNormalized<Duration>(
      millis / kMillisPerSecond,
      (millis % kMillisPerSecond) * kNanosPerMillisecond);
}

Duration TimeUtil::SecondsToDuration(arc_i64 seconds) {
  return CreateNormalized<Duration>(seconds, 0);
}

Duration TimeUtil::MinutesToDuration(arc_i64 minutes) {
  Y_ABSL_DCHECK(minutes >= TimeUtil::kDurationMinSeconds / kSecondsPerMinute &&
              minutes <= TimeUtil::kDurationMaxSeconds / kSecondsPerMinute)
      << "Duration minutes are outside of the valid range";
  return SecondsToDuration(minutes * kSecondsPerMinute);
}

Duration TimeUtil::HoursToDuration(arc_i64 hours) {
  Y_ABSL_DCHECK(hours >= TimeUtil::kDurationMinSeconds / kSecondsPerHour &&
              hours <= TimeUtil::kDurationMaxSeconds / kSecondsPerHour)
      << "Duration hours are outside of the valid range";
  return SecondsToDuration(hours * kSecondsPerHour);
}

arc_i64 TimeUtil::DurationToNanoseconds(const Duration& duration) {
  Y_ABSL_DCHECK(IsDurationValid(duration))
      << "Duration is outside of the valid range";
  return duration.seconds() * kNanosPerSecond + duration.nanos();
}

arc_i64 TimeUtil::DurationToMicroseconds(const Duration& duration) {
  return RoundTowardZero(DurationToNanoseconds(duration), kNanosPerMicrosecond);
}

arc_i64 TimeUtil::DurationToMilliseconds(const Duration& duration) {
  return RoundTowardZero(DurationToNanoseconds(duration), kNanosPerMillisecond);
}

arc_i64 TimeUtil::DurationToSeconds(const Duration& duration) {
  Y_ABSL_DCHECK(IsDurationValid(duration))
      << "Duration is outside of the valid range";
  return duration.seconds();
}

arc_i64 TimeUtil::DurationToMinutes(const Duration& duration) {
  return RoundTowardZero(DurationToSeconds(duration), kSecondsPerMinute);
}

arc_i64 TimeUtil::DurationToHours(const Duration& duration) {
  return RoundTowardZero(DurationToSeconds(duration), kSecondsPerHour);
}

Timestamp TimeUtil::NanosecondsToTimestamp(arc_i64 nanos) {
  return CreateNormalized<Timestamp>(nanos / kNanosPerSecond,
                                     nanos % kNanosPerSecond);
}

Timestamp TimeUtil::MicrosecondsToTimestamp(arc_i64 micros) {
  return CreateNormalized<Timestamp>(
      micros / kMicrosPerSecond,
      micros % kMicrosPerSecond * kNanosPerMicrosecond);
}

Timestamp TimeUtil::MillisecondsToTimestamp(arc_i64 millis) {
  return CreateNormalized<Timestamp>(
      millis / kMillisPerSecond,
      millis % kMillisPerSecond * kNanosPerMillisecond);
}

Timestamp TimeUtil::SecondsToTimestamp(arc_i64 seconds) {
  return CreateNormalized<Timestamp>(seconds, 0);
}

arc_i64 TimeUtil::TimestampToNanoseconds(const Timestamp& timestamp) {
  Y_ABSL_DCHECK(IsTimestampValid(timestamp))
      << "Timestamp is outside of the valid range";
  return timestamp.seconds() * kNanosPerSecond + timestamp.nanos();
}

arc_i64 TimeUtil::TimestampToMicroseconds(const Timestamp& timestamp) {
  Y_ABSL_DCHECK(IsTimestampValid(timestamp))
      << "Timestamp is outside of the valid range";
  return timestamp.seconds() * kMicrosPerSecond +
         RoundTowardZero(timestamp.nanos(), kNanosPerMicrosecond);
}

arc_i64 TimeUtil::TimestampToMilliseconds(const Timestamp& timestamp) {
  Y_ABSL_DCHECK(IsTimestampValid(timestamp))
      << "Timestamp is outside of the valid range";
  return timestamp.seconds() * kMillisPerSecond +
         RoundTowardZero(timestamp.nanos(), kNanosPerMillisecond);
}

arc_i64 TimeUtil::TimestampToSeconds(const Timestamp& timestamp) {
  Y_ABSL_DCHECK(IsTimestampValid(timestamp))
      << "Timestamp is outside of the valid range";
  return timestamp.seconds();
}

Timestamp TimeUtil::TimeTToTimestamp(time_t value) {
  return CreateNormalized<Timestamp>(static_cast<arc_i64>(value), 0);
}

time_t TimeUtil::TimestampToTimeT(const Timestamp& value) {
  return static_cast<time_t>(value.seconds());
}

Timestamp TimeUtil::TimevalToTimestamp(const timeval& value) {
  return CreateNormalized<Timestamp>(value.tv_sec,
                                     value.tv_usec * kNanosPerMicrosecond);
}

timeval TimeUtil::TimestampToTimeval(const Timestamp& value) {
  timeval result;
  result.tv_sec = value.seconds();
  result.tv_usec = RoundTowardZero(value.nanos(), kNanosPerMicrosecond);
  return result;
}

Duration TimeUtil::TimevalToDuration(const timeval& value) {
  return CreateNormalized<Duration>(value.tv_sec,
                                    value.tv_usec * kNanosPerMicrosecond);
}

timeval TimeUtil::DurationToTimeval(const Duration& value) {
  timeval result;
  result.tv_sec = value.seconds();
  result.tv_usec = RoundTowardZero(value.nanos(), kNanosPerMicrosecond);
  // timeval.tv_usec's range is [0, 1000000)
  if (result.tv_usec < 0) {
    result.tv_sec -= 1;
    result.tv_usec += kMicrosPerSecond;
  }
  return result;
}

}  // namespace util
}  // namespace protobuf
}  // namespace google

namespace google {
namespace protobuf {
namespace {
using ::PROTOBUF_NAMESPACE_ID::util::CreateNormalized;
using ::PROTOBUF_NAMESPACE_ID::util::kNanosPerSecond;

// Convert a Duration to uint128.
void ToUint128(const Duration& value, y_absl::uint128* result, bool* negative) {
  if (value.seconds() < 0 || value.nanos() < 0) {
    *negative = true;
    *result = static_cast<arc_ui64>(-value.seconds());
    *result = *result * kNanosPerSecond + static_cast<arc_ui32>(-value.nanos());
  } else {
    *negative = false;
    *result = static_cast<arc_ui64>(value.seconds());
    *result = *result * kNanosPerSecond + static_cast<arc_ui32>(value.nanos());
  }
}

void ToDuration(const y_absl::uint128& value, bool negative, Duration* duration) {
  arc_i64 seconds =
      static_cast<arc_i64>(y_absl::Uint128Low64(value / kNanosPerSecond));
  arc_i32 nanos =
      static_cast<arc_i32>(y_absl::Uint128Low64(value % kNanosPerSecond));
  if (negative) {
    seconds = -seconds;
    nanos = -nanos;
  }
  duration->set_seconds(seconds);
  duration->set_nanos(nanos);
}
}  // namespace

Duration& operator+=(Duration& d1, const Duration& d2) {
  d1 = CreateNormalized<Duration>(d1.seconds() + d2.seconds(),
                                  d1.nanos() + d2.nanos());
  return d1;
}

Duration& operator-=(Duration& d1, const Duration& d2) {  // NOLINT
  d1 = CreateNormalized<Duration>(d1.seconds() - d2.seconds(),
                                  d1.nanos() - d2.nanos());
  return d1;
}

Duration& operator*=(Duration& d, arc_i64 r) {  // NOLINT
  bool negative;
  y_absl::uint128 value;
  ToUint128(d, &value, &negative);
  if (r > 0) {
    value *= static_cast<arc_ui64>(r);
  } else {
    negative = !negative;
    value *= static_cast<arc_ui64>(-r);
  }
  ToDuration(value, negative, &d);
  return d;
}

Duration& operator*=(Duration& d, double r) {  // NOLINT
  double result =
      (static_cast<double>(d.seconds()) + d.nanos() * (1.0 / kNanosPerSecond)) *
      r;
  arc_i64 seconds = static_cast<arc_i64>(result);
  arc_i32 nanos = static_cast<arc_i32>((result - static_cast<double>(seconds)) *
                                       kNanosPerSecond);
  // Note that we normalize here not just because nanos can have a different
  // sign from seconds but also that nanos can be any arbitrary value when
  // overflow happens (i.e., the result is a much larger value than what
  // int64 can represent).
  d = CreateNormalized<Duration>(seconds, nanos);
  return d;
}

Duration& operator/=(Duration& d, arc_i64 r) {  // NOLINT
  bool negative;
  y_absl::uint128 value;
  ToUint128(d, &value, &negative);
  if (r > 0) {
    value /= static_cast<arc_ui64>(r);
  } else {
    negative = !negative;
    value /= static_cast<arc_ui64>(-r);
  }
  ToDuration(value, negative, &d);
  return d;
}

Duration& operator/=(Duration& d, double r) {  // NOLINT
  return d *= 1.0 / r;
}

Duration& operator%=(Duration& d1, const Duration& d2) {  // NOLINT
  bool negative1, negative2;
  y_absl::uint128 value1, value2;
  ToUint128(d1, &value1, &negative1);
  ToUint128(d2, &value2, &negative2);
  y_absl::uint128 result = value1 % value2;
  // When negative values are involved in division, we round the division
  // result towards zero. With this semantics, sign of the remainder is the
  // same as the dividend. For example:
  //     -5 / 10    = 0, -5 % 10    = -5
  //     -5 / (-10) = 0, -5 % (-10) = -5
  //      5 / (-10) = 0,  5 % (-10) = 5
  ToDuration(result, negative1, &d1);
  return d1;
}

arc_i64 operator/(const Duration& d1, const Duration& d2) {
  bool negative1, negative2;
  y_absl::uint128 value1, value2;
  ToUint128(d1, &value1, &negative1);
  ToUint128(d2, &value2, &negative2);
  arc_i64 result = y_absl::Uint128Low64(value1 / value2);
  if (negative1 != negative2) {
    result = -result;
  }
  return result;
}

Timestamp& operator+=(Timestamp& t, const Duration& d) {  // NOLINT
  t = CreateNormalized<Timestamp>(t.seconds() + d.seconds(),
                                  t.nanos() + d.nanos());
  return t;
}

Timestamp& operator-=(Timestamp& t, const Duration& d) {  // NOLINT
  t = CreateNormalized<Timestamp>(t.seconds() - d.seconds(),
                                  t.nanos() - d.nanos());
  return t;
}

Duration operator-(const Timestamp& t1, const Timestamp& t2) {
  return CreateNormalized<Duration>(t1.seconds() - t2.seconds(),
                                    t1.nanos() - t2.nanos());
}
}  // namespace protobuf
}  // namespace google
