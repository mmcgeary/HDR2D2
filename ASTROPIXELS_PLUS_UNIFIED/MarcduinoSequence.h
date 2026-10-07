// RC and Wi-Fi sequence commands share the same timers, audio and servo owner.
MARCDUINO_ACTION(StopSequence, :SE00, ({
    stopDomeMotion();
    cancelR2Macro();
}))

MARCDUINO_ACTION(ScreamSequence, :SE01, ({
    startR2Macro(R2_SCREAM);
}))

MARCDUINO_ACTION(BeepCantinaSequence, :SE05, ({
    startR2Macro(R2_CANTINA);
}))

MARCDUINO_ACTION(ShortSequence, :SE06, ({
    startR2Macro(R2_FAINT);
}))

MARCDUINO_ACTION(CantinaSequence, :SE07, ({
    startR2Macro(R2_CANTINA);
}))

MARCDUINO_ACTION(LeiaMessage, :SE08, ({
    startDomeHoming(R2_LEIA);
}))

MARCDUINO_ACTION(DiscoSequence, :SE09, ({
    startR2Macro(R2_DISCO);
}))
