#include "../../EnergyMetrics.h"
#include "../../EnvoyEnergySample.h"
#include "../harness/test_runner.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace MetricsTests {
using energy_metrics::Counter;
using energy_metrics::Control;

static void near(double actual, double expected) {
  ASSERT_TRUE(std::abs(actual - expected) < 1e-9);
}

static void run(Counter& counter, uint32_t now, double power,
                uint64_t stamp, Control control = {true, 1500, 1500}) {
  counter.sample(now, stamp, power);
  counter.setControl(now, control);
}

TEST_CASE(Metrics, ConstantAndVariableSteps) {
  Counter counter;
  run(counter, 0, 900, 100);
  const uint32_t steps[] = {137, 463, 799, 1000, 1203, 1555, 1800, 2000};
  for (uint32_t now : steps) {
    counter.advance(now);
    if (now == 1000) counter.sample(now, 101, 900);
  }
  near(counter.importWh, 0.5);
  near(counter.exportWh, 0);
  near(counter.gridW, 900);
}

TEST_CASE(Metrics, AlternatingSigns) {
  Counter counter;
  run(counter, 0, 1000, 100);
  for (uint32_t now = 100; now <= 2000; now += 100) {
    counter.sample(now, 100 + now, now % 200 == 0 ? 1000 : -1000);
  }
  near(counter.importWh, 1000.0 / 3600);
  near(counter.exportWh, 1000.0 / 3600);
  Counter exporter;
  run(exporter, 0, -900, 100);
  exporter.advance(1000);
  exporter.advance(2000);
  near(exporter.exportWh, 0.5);
  near(exporter.importWh, 0);
}

TEST_CASE(Metrics, OpeningsAndBounds) {
  Control group;
  ASSERT_TRUE(energy_metrics::addResistiveOutput(group, 1000, 0, true));
  ASSERT_TRUE(energy_metrics::addResistiveOutput(group, 1000, 50, true));
  ASSERT_TRUE(energy_metrics::addResistiveOutput(group, 1000, 100, true));
  near(group.commandedW, 1500);
  near(group.headroomW, 1500);
  ASSERT_TRUE(!energy_metrics::addResistiveOutput(group, 0, 50, true));
  ASSERT_TRUE(!energy_metrics::addResistiveOutput(group, 1000, 50, false));
  ASSERT_TRUE(!energy_metrics::addResistiveOutput(group, 1000, -1, true));
  ASSERT_TRUE(!energy_metrics::addResistiveOutput(group, 1000, 101, true));
  near(group.commandedW, 1500);
  for (double fraction : {0.0, 0.4, 1.0}) {
    for (double power : {-5000.0, 5000.0}) {
      Counter counter;
      run(counter, 0, power, 100, {true, 3000 * fraction, 3000 * (1 - fraction)});
      counter.advance(1000);
      counter.advance(2000);
      near(counter.importWh, power > 0 ? 3000 * fraction / 1800 : 0);
      near(counter.exportWh, power < 0 ? 3000 * (1 - fraction) / 1800 : 0);
    }
  }
}

TEST_CASE(Metrics, Transitions) {
  Counter counter;
  run(counter, 0, 3000, 100, {true, 0, 3000});
  counter.setControl(1000, {true, 3000, 0});
  counter.advance(2000);
  near(counter.importWh, 1500.0 / 1800);
  Counter forced;
  run(forced, 0, 900, 100);
  forced.setControl(1000, {});
  forced.advance(2000);
  near(forced.importWh, 0.25);
  forced.setControl(2000, {true, 1500, 1500});
  forced.sample(2000, 102, 900);
  forced.advance(3000);
  forced.advance(4000);
  near(forced.importWh, 0.75);
}

TEST_CASE(Metrics, StaleAndInvalid) {
  Counter counter;
  run(counter, 0, 900, 100);
  counter.advance(1000);
  counter.advance(2000);
  counter.advance(2500);
  counter.advance(3100);
  ASSERT_TRUE(!counter.fresh(3100));
  near(counter.importWh, 0.625);
  counter.advance(10000);
  ASSERT_TRUE(counter.sample(10000, 110, 900));
  counter.advance(11000);
  counter.advance(12000);
  near(counter.importWh, 1.125);
  ASSERT_TRUE(!counter.sample(12000, 109, 900));
  ASSERT_TRUE(!counter.sample(12001, 111, NAN));
  near(counter.importWh, 1.125);
  Counter gap;
  run(gap, 0, 900, 100);
  gap.advance(1500);
  near(gap.importWh, 0);
  ASSERT_TRUE(!gap.fresh(1500));
  Counter late;
  run(late, 100, 900, 100);
  late.advance(200);
  ASSERT_TRUE(late.sample(199, 101, 900));
  late.advance(1000);
  late.advance(2000);
  late.advance(2100);
  near(late.importWh, 0.5);
}

TEST_CASE(Metrics, SameSecondUpdates) {
  Counter counter;
  run(counter, 0, 1000, 100);
  ASSERT_TRUE(counter.sample(500, 100, -500));
  counter.advance(1000);
  near(counter.importWh, 1000 * 0.5 / 3600);
  near(counter.exportWh, 500 * 0.5 / 3600);
  near(counter.gridW, -500);
  ASSERT_TRUE(counter.sample(1000, 101, -500));
  ASSERT_TRUE(counter.sourcePeriodMs == 1000);
  ASSERT_TRUE(counter.sample(1500, 101, -500));
  ASSERT_TRUE(counter.fresh(4000));
  counter.advance(2000);
  near(counter.exportWh, 500 * 1.5 / 3600);
}

TEST_CASE(Metrics, TriphaseGlobalPower) {
  EnvoyEnergySample sample;
  ASSERT_TRUE(ParseEnvoyReadingSample(
    R"({"timestamp":124,"activePower":-300,
        "channels":[{"activePower":600},{"activePower":-500},{"activePower":-400}]})",
    sample));
  near(sample.powerW, -300);
  Counter counter;
  run(counter, 0, sample.powerW, sample.stamp);
  counter.advance(500);
  near(counter.importWh, 0);
  near(counter.exportWh, 300 * 0.5 / 3600);
}

TEST_CASE(Metrics, RolloverAndRestart) {
  Counter counter;
  const uint32_t start = UINT32_MAX - 999;
  run(counter, start, 900, 100);
  counter.advance(start + 1000);
  counter.advance(start + 2000);
  near(counter.importWh, 0.5);
  Counter restarted;
  near(restarted.importWh, 0);
  near(restarted.exportWh, 0);
  ASSERT_TRUE(!restarted.fresh(0));
}

TEST_CASE(Metrics, Parsing) {
  EnvoyEnergySample sample;
  const uint64_t epoch = 1700000000;
  ASSERT_TRUE(EnvoyEnergySampleFresh({epoch, 0}, epoch, 36000));
  ASSERT_TRUE(EnvoyEnergySampleFresh({epoch, 100}, epoch + 3, 36000));
  ASSERT_TRUE(!EnvoyEnergySampleFresh({epoch, 100}, epoch + 4, 36000));
  ASSERT_TRUE(!EnvoyEnergySampleFresh({epoch + 3, 100}, epoch, 36000));
  ASSERT_TRUE(!EnvoyEnergySampleFresh({epoch, NAN}, epoch, 36000));
  ASSERT_TRUE(!EnvoyEnergySampleFresh({epoch, 40000}, epoch, 36000));
  ASSERT_TRUE(!EnvoyEnergySampleFresh({epoch, 100}, 0, 36000));
  ASSERT_TRUE(ParseEnvoyReadingSample(
    R"({"eid":704643584,"timestamp":124,"activePower":-123.5})", sample));
  ASSERT_TRUE(sample.stamp == 124);
  near(sample.powerW, -123.5);
  ASSERT_TRUE(!ParseEnvoyReadingSample(R"({"activePower":100})", sample));
  ASSERT_TRUE(!ParseEnvoyReadingSample(R"({"timestamp":124})", sample));
  ASSERT_TRUE(!ParseEnvoyReadingSample(R"({"timestamp":124,"activePower":"100"})", sample));
  double phasePower = 0;
  ASSERT_TRUE(ParseEnvoyPhasePower(R"({"activePower":321.5,"voltage":230})", phasePower));
  near(phasePower, 321.5);
  ASSERT_TRUE(!ParseEnvoyPhasePower(R"({"voltage":230})", phasePower));
  ASSERT_TRUE(!ParseEnvoyPhasePower(R"({"activePower":"321.5"})", phasePower));
  ASSERT_TRUE(ParseEnvoyPhasePower(R"({"activePower":0})", phasePower));
  near(phasePower, 0);
  ASSERT_TRUE(ParseEnvoyEnergySample(
    R"([{"reportType":"total-consumption","createdDate":123,"cumulative":{"actPower":999}},
        {"reportType":"net-consumption","createdDate":124,"cumulative":{"actPower":-123.5},
         "lines":[{"actPower":9999}]}])", sample));
  ASSERT_TRUE(sample.stamp == 124);
  near(sample.powerW, -123.5);
  for (const char* bad : {"", "{}", "[]",
       R"([{"reportType":"net-consumption","cumulative":{"actPower":10}}])",
       R"([{"reportType":"net-consumption","createdDate":124,"cumulative":{}}])",
       R"([{"reportType":"net-consumption","createdDate":124,"cumulative":{"actPower":"10"}}])"}) {
    ASSERT_TRUE(!ParseEnvoyEnergySample(bad, sample));
  }
  ASSERT_TRUE(ParseEnvoyEnergySample(
    R"([{"reportType":"net-consumption","createdDate":124,"cumulative":{"actPower":0}}])", sample));
  near(sample.powerW, 0);
}

TEST_CASE(Metrics, FreshnessAndLoopGapExactBoundaries) {
  Counter counter;
  run(counter, 0, -900, 100);
  for (uint32_t now : {1000u, 2000u, 3000u}) counter.advance(now);
  ASSERT_TRUE(counter.fresh(3000));
  near(counter.exportWh, 0.75);
  counter.advance(3001);
  ASSERT_FALSE(counter.fresh(3001));
  near(counter.exportWh, 0.75);
  near(counter.gridW, 0);
  near(counter.exportW, 0);
  ASSERT_TRUE(counter.sample(10000, 110, -900));
  counter.advance(11000);
  near(counter.exportWh, 1);
  Counter gap;
  run(gap, 0, 900, 100);
  gap.advance(1001);
  ASSERT_FALSE(gap.fresh(1001));
  near(gap.importWh, 0);
}

TEST_CASE(Metrics, InvalidReadingClearsDiagnosticsWithoutResettingTotals) {
  for (double invalid : {NAN, INFINITY, -INFINITY}) {
    Counter counter;
    run(counter, 0, 900, 100);
    counter.advance(1000);
    ASSERT_FALSE(counter.sample(1000, 101, invalid));
    near(counter.importWh, 0.25);
    near(counter.gridW, 0);
    near(counter.importW, 0);
    ASSERT_TRUE(counter.sample(1000, 102, 900));
    counter.advance(2000);
    near(counter.importWh, 0.5);
  }
  Counter counter;
  run(counter, 0, -900, 100);
  counter.advance(1000);
  ASSERT_FALSE(counter.sample(1000, 0, -900));
  ASSERT_FALSE(counter.sample(1000, 99, -900));
  near(counter.exportWh, 0.25);
}

TEST_CASE(Metrics, SourcePeriodAndDiagnosticZeroOnIneligibility) {
  Counter counter;
  run(counter, 0, 900, 1700000000);
  counter.sample(500, 1700000000, 800);
  ASSERT_EQ(0u, counter.sourcePeriodMs);
  counter.sample(1000, 1700000002, 700);
  ASSERT_EQ(2000u, counter.sourcePeriodMs);
  counter.sample(1500, 1700000002, -600);
  ASSERT_EQ(2000u, counter.sourcePeriodMs);
  counter.setControl(1500, {});
  near(counter.gridW, -600);
  near(counter.importW, 0);
  near(counter.exportW, 0);
  counter.invalidate();
  near(counter.gridW, 0);
  counter.sample(2000, 1705000000, 700);
  ASSERT_EQ(0u, counter.sourcePeriodMs);
}

TEST_CASE(Metrics, SourceAgeAndPowerExactBoundaries) {
  const uint64_t epoch = 1700000000;
  for (double watts : {-36000.0, 0.0, 36000.0}) {
    ASSERT_TRUE(EnvoyEnergySampleFresh({epoch, watts}, epoch, 36000));
    ASSERT_TRUE(EnvoyEnergySampleFresh({epoch - 3, watts}, epoch, 36000));
    ASSERT_TRUE(EnvoyEnergySampleFresh({epoch + 2, watts}, epoch, 36000));
  }
  ASSERT_FALSE(EnvoyEnergySampleFresh({epoch - 4, 0}, epoch, 36000));
  ASSERT_FALSE(EnvoyEnergySampleFresh({epoch + 3, 0}, epoch, 36000));
  ASSERT_FALSE(EnvoyEnergySampleFresh({epoch, 36000.01}, epoch, 36000));
  ASSERT_FALSE(EnvoyEnergySampleFresh({epoch, -36000.01}, epoch, 36000));
  ASSERT_FALSE(EnvoyEnergySampleFresh({0, 0}, epoch, 36000));
}

TEST_CASE(Metrics, HeaderPhaseAndMalformedPayloadValidation) {
  EnvoyEnergySample sample;
  ASSERT_TRUE(ParseEnvoyReadingHeader(
    "},{\"timestamp\":1700000000,\"activePower\":-300, \r\n\t \"channels\":[", sample));
  ASSERT_EQ(uint64_t(1700000000), sample.stamp);
  near(sample.powerW, -300);
  ASSERT_TRUE(ParseEnvoyReadingHeader(
    "{\"timestamp\":1700000000,\"activePower\":0,\"channels\":[]}", sample));
  for (const char* bad : {
      "", "{}", "{\"channels\":[]}", "{\"timestamp\":0,\"activePower\":0,\"channels\":[",
      "{\"timestamp\":-1,\"activePower\":0,\"channels\":[",
      "{\"timestamp\":1.5,\"activePower\":0,\"channels\":[",
      "{\"timestamp\":\"1700000000\",\"activePower\":0,\"channels\":[",
      "{\"timestamp\":1700000000,\"activePower\":null,\"channels\":[",
      "{\"timestamp\":1700000000,\"channels\":["
    }) {
    ASSERT_FALSE(ParseEnvoyReadingHeader(bad, sample));
  }
  const std::string large = "{\"padding\":\"" + std::string(1100, 'x') +
    "\",\"timestamp\":1700000000,\"activePower\":0,\"channels\":[";
  ASSERT_FALSE(ParseEnvoyReadingHeader(large.c_str(), sample));
  double watts = 0;
  for (const char* bad : {"", "null", "{\"activePower\":null}", "{\"activePower\":true}",
                          "{\"activePower\":1e999}", "{\"activePower\":NaN}"})
    ASSERT_FALSE(ParseEnvoyPhasePower(bad, watts));
}

static std::string readUntil(const std::string& body, size_t& pos, char end) {
  const size_t stop = body.find(end, pos);
  ASSERT_TRUE(stop != std::string::npos);
  std::string chunk = body.substr(pos, stop + 1 - pos);
  pos = stop + 1;
  return chunk;
}

// Slices the real pretty-printed Envoy capture exactly like LectureEnphase does.
TEST_CASE(Metrics, RealPrettyPrintedEnvoyBlocks) {
  std::ifstream file("tests/enphase/fixtures/envoy_readings_real_8meters.json");
  ASSERT_TRUE(file.is_open());
  const std::string body((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  size_t pos = body.find('[') + 1;
  std::string header, phase;
  for (int meter = 0; meter < 2; ++meter) {
    header = readUntil(body, pos, '[');
    ASSERT_TRUE(header.size() < 1024);
    for (int i = 0; i < 3; ++i) {
      std::string current = readUntil(body, pos, '}');
      ASSERT_TRUE(current.size() < 1024);
      if (i == 0) phase = current;
    }
  }
  EnvoyEnergySample sample;
  ASSERT_TRUE(ParseEnvoyReadingHeader(header.c_str(), sample));
  ASSERT_EQ(uint64_t(1791895104), sample.stamp);
  ASSERT_TRUE(std::abs(sample.powerW + 92.077) < 1e-3);
  double watts = 0;
  ASSERT_TRUE(ParseEnvoyPhasePower(phase.c_str(), watts));
  ASSERT_TRUE(std::abs(watts - 1128.769) < 1e-3);
}

TEST_CASE(Metrics, OneHourAgainstIndependentIntervalOracle) {
  Counter counter;
  Control control = {true, 1200, 1800};
  double watts = -800, expectedImport = 0, expectedExport = 0;
  uint32_t now = 0;
  uint32_t seed = 12345;
  run(counter, now, watts, 1700000000, control);
  for (int tick = 1; tick <= 18000; tick++) {
    now += 200;
    if (control.eligible) {
      expectedImport += std::min(std::max(watts, 0.0), control.commandedW) / 18000;
      expectedExport += std::min(std::max(-watts, 0.0), control.headroomW) / 18000;
    }
    counter.advance(now);
    if (tick % 10 == 0) {
      seed = seed * 1664525u + 1013904223u;
      watts = int(seed % 10001) - 5000;
      counter.sample(now, 1700000000 + now / 1000, watts);
    }
    if (tick % 17 == 0) {
      const double opening = (tick % 101) / 100.0;
      control = {tick % 119 != 0, 3000 * opening, 3000 * (1 - opening)};
      counter.setControl(now, control);
    }
    ASSERT_TRUE(counter.importWh >= 0 && counter.exportWh >= 0);
  }
  ASSERT_NEAR(expectedImport, counter.importWh, 1e-8);
  ASSERT_NEAR(expectedExport, counter.exportWh, 1e-8);
}

} // namespace MetricsTests
