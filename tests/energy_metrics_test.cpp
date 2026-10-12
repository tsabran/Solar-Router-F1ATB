#include "../EnergyMetrics.h"
#include "../EnvoyEnergySample.h"
#include <cassert>
#include <iostream>

using energy_metrics::Counter;
using energy_metrics::Control;

static void near(double actual, double expected) {
  assert(std::abs(actual - expected) < 1e-9);
}

static void run(Counter& counter, uint32_t now, double power,
                uint64_t stamp, Control control = {true, 1500, 1500}) {
  counter.sample(now, stamp, power);
  counter.setControl(now, control);
}

static void constantAndVariableSteps() {
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

static void alternatingSigns() {
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

static void openingsAndBounds() {
  Control group;
  assert(energy_metrics::addResistiveOutput(group, 1000, 0, true));
  assert(energy_metrics::addResistiveOutput(group, 1000, 50, true));
  assert(energy_metrics::addResistiveOutput(group, 1000, 100, true));
  near(group.commandedW, 1500);
  near(group.headroomW, 1500);
  assert(!energy_metrics::addResistiveOutput(group, 0, 50, true));
  assert(!energy_metrics::addResistiveOutput(group, 1000, 50, false));
  assert(!energy_metrics::addResistiveOutput(group, 1000, -1, true));
  assert(!energy_metrics::addResistiveOutput(group, 1000, 101, true));
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

static void transitions() {
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

static void staleAndInvalid() {
  Counter counter;
  run(counter, 0, 900, 100);
  counter.advance(1000);
  counter.advance(2000);
  counter.advance(2500);
  counter.advance(3100);
  assert(!counter.fresh(3100));
  near(counter.importWh, 0.625);
  counter.advance(10000);
  assert(counter.sample(10000, 110, 900));
  counter.advance(11000);
  counter.advance(12000);
  near(counter.importWh, 1.125);
  assert(!counter.sample(12000, 109, 900));
  assert(!counter.sample(12001, 111, NAN));
  near(counter.importWh, 1.125);
  Counter gap;
  run(gap, 0, 900, 100);
  gap.advance(1500);
  near(gap.importWh, 0);
  assert(!gap.fresh(1500));
  Counter late;
  run(late, 100, 900, 100);
  late.advance(200);
  assert(late.sample(199, 101, 900));
  late.advance(1000);
  late.advance(2000);
  late.advance(2100);
  near(late.importWh, 0.5);
}

static void sameSecondUpdates() {
  Counter counter;
  run(counter, 0, 1000, 100);
  assert(counter.sample(500, 100, -500));
  counter.advance(1000);
  near(counter.importWh, 1000 * 0.5 / 3600);
  near(counter.exportWh, 500 * 0.5 / 3600);
  near(counter.gridW, -500);
  assert(counter.sample(1000, 101, -500));
  assert(counter.sourcePeriodMs == 1000);
  assert(counter.sample(1500, 101, -500));
  assert(counter.fresh(4000));
  counter.advance(2000);
  near(counter.exportWh, 500 * 1.5 / 3600);
}

static void triphaseGlobalPower() {
  EnvoyEnergySample sample;
  assert(ParseEnvoyReadingSample(
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

static void rolloverAndRestart() {
  Counter counter;
  const uint32_t start = UINT32_MAX - 999;
  run(counter, start, 900, 100);
  counter.advance(start + 1000);
  counter.advance(start + 2000);
  near(counter.importWh, 0.5);
  Counter restarted;
  near(restarted.importWh, 0);
  near(restarted.exportWh, 0);
  assert(!restarted.fresh(0));
}

static void parsing() {
  EnvoyEnergySample sample;
  const uint64_t epoch = 1700000000;
  assert(EnvoyEnergySampleFresh({epoch, 0}, epoch, 36000));
  assert(EnvoyEnergySampleFresh({epoch, 100}, epoch + 3, 36000));
  assert(!EnvoyEnergySampleFresh({epoch, 100}, epoch + 4, 36000));
  assert(!EnvoyEnergySampleFresh({epoch + 3, 100}, epoch, 36000));
  assert(!EnvoyEnergySampleFresh({epoch, NAN}, epoch, 36000));
  assert(!EnvoyEnergySampleFresh({epoch, 40000}, epoch, 36000));
  assert(!EnvoyEnergySampleFresh({epoch, 100}, 0, 36000));
  assert(ParseEnvoyReadingSample(
    R"({"eid":704643584,"timestamp":124,"activePower":-123.5})", sample));
  assert(sample.stamp == 124);
  near(sample.powerW, -123.5);
  assert(!ParseEnvoyReadingSample(R"({"activePower":100})", sample));
  assert(!ParseEnvoyReadingSample(R"({"timestamp":124})", sample));
  assert(!ParseEnvoyReadingSample(R"({"timestamp":124,"activePower":"100"})", sample));
  double phasePower = 0;
  assert(ParseEnvoyPhasePower(R"({"activePower":321.5,"voltage":230})", phasePower));
  near(phasePower, 321.5);
  assert(!ParseEnvoyPhasePower(R"({"voltage":230})", phasePower));
  assert(!ParseEnvoyPhasePower(R"({"activePower":"321.5"})", phasePower));
  assert(ParseEnvoyPhasePower(R"({"activePower":0})", phasePower));
  near(phasePower, 0);
  assert(ParseEnvoyEnergySample(
    R"([{"reportType":"total-consumption","createdDate":123,"cumulative":{"actPower":999}},
        {"reportType":"net-consumption","createdDate":124,"cumulative":{"actPower":-123.5},
         "lines":[{"actPower":9999}]}])", sample));
  assert(sample.stamp == 124);
  near(sample.powerW, -123.5);
  for (const char* bad : {"", "{}", "[]",
       R"([{"reportType":"net-consumption","cumulative":{"actPower":10}}])",
       R"([{"reportType":"net-consumption","createdDate":124,"cumulative":{}}])",
       R"([{"reportType":"net-consumption","createdDate":124,"cumulative":{"actPower":"10"}}])"}) {
    assert(!ParseEnvoyEnergySample(bad, sample));
  }
  assert(ParseEnvoyEnergySample(
    R"([{"reportType":"net-consumption","createdDate":124,"cumulative":{"actPower":0}}])", sample));
  near(sample.powerW, 0);
}

int main() {
  constantAndVariableSteps();
  alternatingSigns();
  openingsAndBounds();
  transitions();
  staleAndInvalid();
  sameSecondUpdates();
  triphaseGlobalPower();
  rolloverAndRestart();
  parsing();
  std::cout << "Energy metrics: all tests passed\n";
}
