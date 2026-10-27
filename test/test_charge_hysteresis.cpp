#include <unity.h>
#include <Preferences.h>
#include "../src/current_taper.h"
#include "../src/protection.h"

namespace {
struct Store { charge::Record record; int writes = 0; bool fail = false; };
bool save(const charge::Record& r, void* ctx) {
    Store& s = *static_cast<Store*>(ctx); ++s.writes;
    if (s.fail) return false;
    s.record = r; return true;
}
void tick(charge::Hysteresis& h, Store& s, uint32_t t, int mv, bool healthy = true, uint32_t fp = 42) {
    h.update(t, mv, 4140, healthy, fp, save, &s);
}
void period(charge::Hysteresis& h, Store& s, uint32_t start, uint32_t end, int mv) {
    for (uint32_t t = start; t <= end; t += 1000) tick(h, s, t, mv);
}
}

void test_hysteresis_boundaries_and_tail() {
    Store s; charge::Hysteresis h; h.restore(nullptr, false, 0);
    tick(h,s,0,4139); TEST_ASSERT_EQUAL_INT(10,h.limit(10,10));
    tick(h,s,1,4140); TEST_ASSERT_EQUAL_INT(0,h.limit(10,10));
    const auto event = h.event();
    period(h,s,1000,299000,4120);
    TEST_ASSERT_EQUAL_INT(0,h.limit(25,10));
    tick(h,s,300001,4120); TEST_ASSERT_EQUAL_INT(10,h.limit(25,10));
    TEST_ASSERT_EQUAL_INT(2,s.writes); TEST_ASSERT_EQUAL_INT(0,h.event());
    period(h,s,301000,360000,4100); TEST_ASSERT_EQUAL_INT(10,h.limit(55,10));
    tick(h,s,361000,4100); TEST_ASSERT_EQUAL_INT(55,h.limit(55,10));
    tick(h,s,361001,4140); TEST_ASSERT_EQUAL_INT(0,h.limit(10,10));
    TEST_ASSERT_TRUE(h.event() > event);
}

void test_hysteresis_health_config_and_wrap() {
    Store s; charge::Hysteresis h; h.restore(nullptr,false,UINT32_MAX-1000);
    tick(h,s,UINT32_MAX-1000,4140);
    for (uint32_t n=1;n<=300;++n) tick(h,s,uint32_t(UINT32_MAX-1000+n*1000),4120);
    TEST_ASSERT_EQUAL_INT(10,h.limit(25,10));
    tick(h,s,300000,4140);
    period(h,s,301000,600000,4130); // relaxation alone never releases
    period(h,s,601000,659000,4120);
    tick(h,s,660000,4121); tick(h,s,661000,4120);
    tick(h,s,662000,4120,false); // stale resets dwell
    period(h,s,663000,722000,4120);
    TEST_ASSERT_EQUAL_INT(0,h.limit(25,10));
    tick(h,s,723000,4120); TEST_ASSERT_EQUAL_INT(10,h.limit(25,10));
    tick(h,s,724000,4140);
    tick(h,s,725000,4139,true,43);
    TEST_ASSERT_EQUAL_INT(0,h.event()); TEST_ASSERT_EQUAL_INT(300000,h.off_remaining());
    tick(h,s,726000,4140,true,43); TEST_ASSERT_TRUE(h.event()!=0);
    TEST_ASSERT_EQUAL_INT(299000,h.off_remaining()); // no shortened hold
    h.restore(nullptr,false,0);
    h.update(0,4140,4140,false,42,save,&s,true); // fresh voltage but protection fault
    TEST_ASSERT_EQUAL_INT(0,h.limit(10,10)); TEST_ASSERT_EQUAL_INT(0,h.event());
    tick(h,s,1000,4139); // recovery below stop is not fresh full evidence
    TEST_ASSERT_EQUAL_INT(0,h.event()); TEST_ASSERT_EQUAL_INT(0,h.limit(10,10));
}

void test_hysteresis_persistence_and_failures() {
    Store s; charge::Hysteresis h; h.restore(nullptr,false,0); tick(h,s,0,4140);
    TEST_ASSERT_TRUE(charge::valid(s.record));
    charge::Hysteresis reboot; reboot.restore(&s.record,false,500000);
    tick(reboot,s,500000,4130); TEST_ASSERT_EQUAL_INT(0,reboot.event());
    period(reboot,s,501000,799000,4120); TEST_ASSERT_EQUAL_INT(0,reboot.limit(25,10));
    tick(reboot,s,800000,4120); TEST_ASSERT_EQUAL_INT(10,reboot.limit(25,10));
    charge::Hysteresis tail; tail.restore(&s.record,false,0);
    tick(tail,s,0,4120); TEST_ASSERT_EQUAL_INT(10,tail.limit(25,10));
    auto corrupt=s.record; ++corrupt.fingerprint;
    tail.restore(&corrupt,false,0); tick(tail,s,0,4000);
    TEST_ASSERT_TRUE(tail.storage_error()); TEST_ASSERT_EQUAL_INT(0,tail.limit(130,10));
    h.restore(nullptr,false,0); s.fail=true; tick(h,s,0,4140);
    TEST_ASSERT_TRUE(h.storage_error()); TEST_ASSERT_EQUAL_INT(0,h.limit(130,10));
    h.restore(nullptr,false,0); s.fail=false; tick(h,s,0,4140);
    period(h,s,1000,299000,4120); s.fail=true; tick(h,s,300000,4120);
    TEST_ASSERT_EQUAL_INT(int(charge::Mode::TOP_HOLD),int(h.mode()));
    TEST_ASSERT_EQUAL_INT(0,h.limit(25,10));
}

void test_hysteresis_chatter_replay() {
    Store s; charge::Hysteresis h; h.restore(nullptr,false,0);
    for (uint32_t t=0;t<600000;t+=1000) {
        tick(h,s,t,t%10000<5000?4140:4137);
        TEST_ASSERT_EQUAL_INT(0,h.limit(10,10));
    }
    TEST_ASSERT_EQUAL_INT(1,s.writes); TEST_ASSERT_EQUAL_INT(1,h.event());
    period(h,s,600000,2400000,4129); TEST_ASSERT_EQUAL_INT(0,h.limit(10,10));
    // A long sampling gap cannot count as a continuous dwell.
    tick(h,s,2401000,4120); tick(h,s,2461000,4120);
    TEST_ASSERT_EQUAL_INT(0,h.limit(25,10));
}

extern unsigned long g_mockMillis;
void test_hysteresis_runtime_snapshot_and_restore() {
    g_bmsSettings.expectedCmusA=1; g_bmsSettings.expectedCmusB=0;
    g_bmsSettings.chargeStopVoltageMv=4140; g_bmsSettings.chargeReducedVoltageMv=4130;
    g_bmsSettings.chargeFullVoltageMv=4050; g_bmsSettings.chargeReducedCurrentDa=10;
    auto& c=g_bmsState.modules[0]; g_mockMillis=100;
    for (int i=0;i<CELLS_PER_MODULE;++i) c.voltages[i]=4140;
    for (int i=0;i<TEMPS_PER_MODULE;++i) c.temperatures[i]=25000;
    c.recordRequiredFrame(MSG_TYPE_STATUS,g_mockMillis);
    c.recordRequiredFrame(MSG_TYPE_VOLTS_1,g_mockMillis);
    c.recordRequiredFrame(MSG_TYPE_VOLTS_2,g_mockMillis);
    protectionCheck(); currentTaperTick();
    const auto first=currentTaperSnapshot();
    TEST_ASSERT_TRUE(first.healthy); TEST_ASSERT_EQUAL_INT(6,first.reason);
    for(int i=0;i<100;++i) TEST_ASSERT_EQUAL_UINT32(first.event,currentTaperSnapshot().event);
    currentTaperInit();
    for(int i=0;i<CELLS_PER_MODULE;++i) c.voltages[i]=4130;
    protectionCheck(); currentTaperTick();
    TEST_ASSERT_EQUAL_INT(7,currentTaperSnapshot().reason);
    TEST_ASSERT_EQUAL_INT(0,currentTaperCalculate().chargeDa);
    TEST_ASSERT_TRUE(currentTaperCalculate().dischargeDa>0);
    c.voltages[0]=4200;
    TEST_ASSERT_TRUE(protectionEnableSupervisedOverride());
    protectionCheck(); currentTaperTick();
    TEST_ASSERT_FALSE(currentTaperSnapshot().healthy);
    TEST_ASSERT_EQUAL_INT(5,currentTaperSnapshot().reason);
    TEST_ASSERT_EQUAL_INT(0,currentTaperCalculate().chargeDa);
    TEST_ASSERT_TRUE(currentTaperCalculate().dischargeDa>0); // voltage recovery preserved
    c.temperatures[0]=70000;
    protectionCheck(); currentTaperTick();
    TEST_ASSERT_EQUAL_INT(0,currentTaperCalculate().dischargeDa);
    g_mockMillis+=1001;
    TEST_ASSERT_FALSE(currentTaperSnapshot().healthy);
    TEST_ASSERT_EQUAL_INT(0,currentTaperCalculate().dischargeDa);
    Preferences p; p.begin("charge_hold",false); p.putUInt("state",5); p.end();
    currentTaperInit(); currentTaperTick();
    TEST_ASSERT_TRUE(currentTaperSnapshot().storageError);
}
