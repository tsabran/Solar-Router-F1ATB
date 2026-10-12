portMUX_TYPE MesureEnergieMux = portMUX_INITIALIZER_UNLOCKED;
EnvoyEnergySample MesureEnergie;
uint32_t MesureEnergieMillis = 0;
uint32_t GenerationMesureEnergie = 0;
bool MesureEnergieValide = false;
bool SuiviMesuresEnergie = false;
bool CommandeEnergieValide = false;
bool ConfigurationEnergieValide = false;
int GroupeEnergie = -1;
const char* EtatEnergie = "disabled";
energy_metrics::Control CommandeEnergie;

bool LectureEnergieActive() {
  portENTER_CRITICAL(&MesureEnergieMux);
  const bool enabled = SuiviMesuresEnergie;
  portEXIT_CRITICAL(&MesureEnergieMux);
  return enabled;
}

void RecevoirEchantillonEnergie(EnvoyEnergySample sample, bool valid) {
  if (!LectureEnergieActive()) return;
  const time_t epoch = time(nullptr);
  // The source timestamp is in seconds, not an HTTP request counter.
  valid = valid && epoch > 0 &&
          EnvoyEnergySampleFresh(sample, uint64_t(epoch), PmaxReseau);
  portENTER_CRITICAL(&MesureEnergieMux);
  MesureEnergie = sample;
  MesureEnergieMillis = millis();
  MesureEnergieValide = valid;
  ++GenerationMesureEnergie;
  portEXIT_CRITICAL(&MesureEnergieMux);
}

void RecevoirMesureEnergie(const char* json) {
  if (!LectureEnergieActive()) return;
  EnvoyEnergySample sample;
  const bool valid = json && ParseEnvoyEnergySample(json, sample);
  RecevoirEchantillonEnergie(sample, valid);
}

void InitialiserObservationEnergie() {
  static const EnvoyReadObserver::Hooks hooks = {
    LectureEnergieActive, ParseEnvoyReadingHeader, ParseEnvoyPhasePower,
    RecevoirEchantillonEnergie
  };
  EnvoyReadObserver::registerHooks(&hooks);
}

void InvaliderCommandeEnergie() {
  portENTER_CRITICAL(&MesureEnergieMux);
  SuiviMesuresEnergie = false;
  portEXIT_CRITICAL(&MesureEnergieMux);
  CommandeEnergieValide = false;
  ConfigurationEnergieValide = false;
  CompteursPotentiels.setControl(millis(), {});
  CompteursPotentiels.invalidate();
}

void ActualiserEnergiePotentielle() {
  static uint32_t generation = 0;
  EnvoyEnergySample sample;
  uint32_t receivedAt, nextGeneration;
  bool valid;
  portENTER_CRITICAL(&MesureEnergieMux);
  sample = MesureEnergie;
  receivedAt = MesureEnergieMillis;
  nextGeneration = GenerationMesureEnergie;
  valid = MesureEnergieValide;
  portEXIT_CRITICAL(&MesureEnergieMux);
  uint32_t now = millis();
  const time_t epoch = time(nullptr);
  valid = valid && epoch > 0 &&
          EnvoyEnergySampleFresh(sample, uint64_t(epoch), PmaxReseau);
  bool forced = testTrame > 0;
  for (int i = 0; i < NbActions; ++i) {
    if (LesActions[i].tOnOff != 0) forced = true;
  }
  if (forced || Source != "Enphase") {
    CompteursPotentiels.setControl(now, {});
    CommandeEnergieValide = false;
    if (forced) EtatEnergie = "forced";
  }
  if (generation != nextGeneration) {
    generation = nextGeneration;
    if (valid) {
      CompteursPotentiels.sample(receivedAt, sample.stamp, sample.powerW);
    } else {
      CompteursPotentiels.invalidate();
    }
  }
  if (!valid) CompteursPotentiels.invalidate();
  CompteursPotentiels.advance(now);
  if (Source != "Enphase") {
    EtatEnergie = "unsupported_source";
  } else if (!CompteursPotentiels.fresh(now) && GroupeEnergie >= 0) {
    EtatEnergie = "invalid_or_stale_measurement";
  }
}

void ActualiserCommandeEnergie() {
  energy_metrics::Control control;
  const char* reason = "disabled";
  int groups = 0;
  int selected = -1;
  for (int i = 0; i < NbActions; ++i) {
    if (LesActions[i].SuiviEnergie) {
      ++groups;
      selected = i;
    }
  }
  GroupeEnergie = selected;
  ConfigurationEnergieValide = false;
  if (groups > 1) {
    reason = "multiple_groups";
  } else if (selected >= 0) {
    reason = "invalid_group";
    if (LesActions[selected].Actif == MODE_SEQUENCEUR &&
        LesActions[selected].IdxSequenceur < 0) {
      bool supported = true;
      int members = 0;
      for (int i = 0; i < NbActions; ++i) {
        if (LesActions[i].IdxSequenceur != selected) continue;
        ++members;
        if (!energy_metrics::addResistiveOutput(control, LesActions[i].PuissanceCharge,
              Retard[i], i > 0 && LesActions[i].Actif == MODE_DEMISINUS && Gpio[i] > 0)) {
          supported = false;
        }
      }
      ConfigurationEnergieValide = supported && members > 0;
      if (ConfigurationEnergieValide) {
        control.eligible = EnergieAutonome[selected];
        reason = control.eligible ? "autonomous" : "not_available_or_scheduled_on";
        const int channel = LesActions[selected].CanalTempEnCours(HeureCouranteDeci);
        if (channel >= 0 && (channel >= 4 || TemperatureValide[channel] <= 0)) {
          control.eligible = false;
          reason = "invalid_temperature";
        }
        for (int i = 0; i < NbActions; ++i) {
          if (LesActions[i].tOnOff != 0) {
            control.eligible = false;
            reason = "forced";
          }
        }
        if (testTrame > 0) {
          control.eligible = false;
          reason = "test_modulation";
        }
      } else {
        reason = "missing_nominal_or_unsupported_output";
      }
    }
  }
  if (Source != "Enphase" && selected >= 0) {
    control.eligible = false;
    reason = "unsupported_source";
  }
  CommandeEnergieValide = control.eligible;
  portENTER_CRITICAL(&MesureEnergieMux);
  SuiviMesuresEnergie = ConfigurationEnergieValide && Source == "Enphase";
  portEXIT_CRITICAL(&MesureEnergieMux);
  CommandeEnergie = control;
  CompteursPotentiels.setControl(millis(), control);
  EtatEnergie = reason;
  static String previousReason;
  if (previousReason != reason) {
    if (selected >= 0 || previousReason != "disabled")
      StockMessage("Suivi energie potentielle : " + String(reason));
    previousReason = reason;
  }
}
