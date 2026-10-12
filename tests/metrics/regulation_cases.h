// Included by the sequencer bench to exercise monitoring on the real controller.
TEST_CASE(MetricsIntegration, MonitoringDoesNotChangeRegulation) {
    auto simulate = [](bool enabled) {
        reset();
        LesActions[1].SuiviEnergie = enabled;
        LesActions[1].Type[0] = 4;
        LesActions[1].Vmin[0] = -20;
        LesActions[1].Vmax[0] = 100;
        LesActions[1].Ki = 20;
        std::vector<double> trace;
        const auto start = millis();
        for (int tick = 0; tick < 300; tick++) {
            simulatedEpoch = 1700000000 + (millis() - start) / 1000;
            if (tick % 10 == 0) {
                const double grid = power() - (tick < 150 ? 1800 : 400);
                PuissanceS_M = std::max(grid, 0.0);
                PuissanceI_M = std::max(-grid, 0.0);
                RecevoirEchantillonEnergie({uint64_t(simulatedEpoch), grid}, true);
            }
            ActualiserEnergiePotentielle();
            GestionOverproduction();
            for (int j = 0; j < NbActions; j++) {
                trace.push_back(Retard[j]);
                trace.push_back(IntegrErrorPw[j]);
                trace.push_back(Propor[j]);
                trace.push_back(DeriveF[j]);
                trace.push_back(PulseOn[j]);
                trace.push_back(PulseTotal[j]);
                trace.push_back(LesActions[j].On);
            }
            trace.push_back(SequencerIO::digital.size());
            trace.push_back(SequencerIO::pwm.size());
            advance_simulated_millis(200);
        }
        if (enabled) {
            ASSERT_TRUE(ConfigurationEnergieValide);
            ASSERT_TRUE(CommandeEnergieValide);
            ASSERT_TRUE(CompteursPotentiels.exportWh > 0);
        } else {
            ASSERT_EQ(0, CompteursPotentiels.exportWh);
        }
        return trace;
    };
    const auto without = simulate(false);
    const auto with = simulate(true);
    ASSERT_TRUE(without == with);
}

TEST_CASE(MetricsIntegration, RealPlanningDeterminesEligibility) {
    reset();
    LesActions[1].SuiviEnergie = true;
    demand(50);
    ASSERT_TRUE(ConfigurationEnergieValide);
    ASSERT_FALSE(CommandeEnergieValide);
    ASSERT_EQ(1500, CommandeEnergie.commandedW);
    ASSERT_EQ(1500, CommandeEnergie.headroomW);
    for (int type : {1, 2, 3, 4}) {
        LesActions[1].Type[0] = type;
        LesActions[1].Vmax[0] = 100;
        GestionOverproduction();
        ASSERT_EQ(type == 3 || type == 4, CommandeEnergieValide);
    }
    LesActions[1].Hdeb[0] = 1300;
    GestionOverproduction();
    ASSERT_FALSE(CommandeEnergieValide);
    LesActions[1].Hdeb[0] = 0;
    LesActions[1].Tarif[0] = 2;
    LTARFbin = 1;
    GestionOverproduction();
    ASSERT_FALSE(CommandeEnergieValide);
    LTARFbin = 2;
    GestionOverproduction();
    ASSERT_TRUE(CommandeEnergieValide);
    LesActions[1].CanalTemp[0] = 0;
    GestionOverproduction();
    ASSERT_FALSE(CommandeEnergieValide);
    TemperatureValide[0] = 1;
    temperature[0] = 40;
    LesActions[1].Tinf[0] = 600;
    LesActions[1].Tsup[0] = 300;
    GestionOverproduction();
    ASSERT_TRUE(CommandeEnergieValide);
    temperature[0] = 70;
    GestionOverproduction();
    ASSERT_FALSE(CommandeEnergieValide);
}

TEST_CASE(MetricsIntegration, IntegerRelayDelaysAndForcedExclusion) {
    reset();
    LesActions[1].SuiviEnergie = true;
    LesActions[2].PuissanceCharge = 500;
    LesActions[3].PuissanceCharge = 1700;
    LesActions[4].PuissanceCharge = 900;
    demand(37);
    double expected = 0;
    for (int j = 2; j < 5; j++)
        expected += LesActions[j].PuissanceCharge * (100 - Retard[j]) / 100.0;
    ASSERT_EQ(expected, CommandeEnergie.commandedW);
    ASSERT_EQ(3100 - expected, CommandeEnergie.headroomW);
    LesActions[1].Type[0] = 4;
    LesActions[1].Vmax[0] = 100;
    GestionOverproduction();
    ASSERT_TRUE(CommandeEnergieValide);
    for (int force : {-60, 60}) {
        LesActions[0].tOnOff = force;
        GestionOverproduction();
        ASSERT_FALSE(CommandeEnergieValide);
        ASSERT_TRUE(ConfigurationEnergieValide);
    }
    LesActions[0].tOnOff = 0;
    GestionOverproduction();
    ASSERT_TRUE(CommandeEnergieValide);
}

TEST_CASE(MetricsIntegration, InvalidParentCannotEnableMonitoring) {
    for (int selected : {0, 1}) {
        reset();
        LesActions[1].SuiviEnergie = false;
        LesActions[selected].Actif = MODE_SEQUENCEUR;
        LesActions[selected].SuiviEnergie = true;
        LesActions[selected].Type[0] = 4;
        LesActions[selected].Vmax[0] = 100;
        if (selected == 1) LesActions[1].IdxSequenceur = -2;
        for (int j = 2; j < 5; j++) LesActions[j].IdxSequenceur = selected;
        GestionOverproduction();
        ASSERT_FALSE(ConfigurationEnergieValide);
        ASSERT_FALSE(CommandeEnergieValide);
    }
}
