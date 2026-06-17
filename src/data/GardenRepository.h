#pragma once
#include "../ui/ui_model.h"
#include "TimeSeriesStore.h"
#include "PumpLog.h"
#include "Clock.h"

// The seam the UI sees. Today: LocalRepository over the on-device store.
// Later: a CachedCloudRepository implements the same interface, UI unchanged.
class IGardenRepository {
public:
  virtual ~IGardenRepository() {}
  virtual UiModel buildModel() = 0;
  virtual void togglePump(int index) = 0;   // Page-3 CONF action
};

class LocalRepository : public IGardenRepository {
public:
  LocalRepository(TimeSeriesStore& store, PumpLog& log, Clock& clock)
    : store_(store), log_(log), clock_(clock) {}
  UiModel buildModel() override;
  void togglePump(int index) override;
private:
  void fillDateTime(UiModel&, uint32_t now);
  void fillEnv(UiModel&, uint32_t now);
  void fillChart(UiModel&, uint32_t now);
  void fillNodes(UiModel&, uint32_t now);
  void fillPumps(UiModel&, uint32_t now);
  bool lastStart(ts::NodeId, uint32_t& ts) const;  // newest START ts for a node
  bool lastStop(ts::NodeId, uint32_t& ts) const;   // newest STOP ts for a node

  TimeSeriesStore& store_;
  PumpLog& log_;
  Clock& clock_;
};
