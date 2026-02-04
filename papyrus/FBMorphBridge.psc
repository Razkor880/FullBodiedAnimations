Scriptname FBMorphBridge Hidden

; RaceMenu / NiOverride provides these native globals.
; This requires that NiOverride.psc is available to compile (RaceMenu).
Import NiOverride

String Function FBKey() Global
    return "FullBodiedAnimations"
EndFunction

Function FBSetMorph(Actor akActor, String morphName, Float value) Global
    if akActor == None
        return
    endif
    if morphName == ""
        return
    endif

    NiOverride.SetBodyMorph(akActor, morphName, FBKey(), value)

    if akActor.Is3DLoaded()
        NiOverride.UpdateModelWeight(akActor)
    endif
EndFunction

Function FBClearMorph(Actor akActor, String morphName) Global
    if akActor == None
        return
    endif
    if morphName == ""
        return
    endif

    NiOverride.ClearBodyMorph(akActor, morphName, FBKey())

    if akActor.Is3DLoaded()
        NiOverride.UpdateModelWeight(akActor)
    endif
EndFunction