Scriptname FBMorphBridge Hidden
Import NiOverride

String Function FBKey() Global
    return "FullBodiedAnimations"
EndFunction

Function FBSetMorph(Actor akActor, String morphName, Float value) Global
    if akActor == None || morphName == ""
        return
    endif

    NiOverride.SetBodyMorph(akActor, morphName, FBKey(), value)
    NiOverride.ApplyMorphs(akActor)
    NiOverride.UpdateModelWeight(akActor)

EndFunction

; NOTE: name matches kFnClear = "FBClearMorph"
Function FBClearMorph(Actor akActor, String morphName) Global
    if akActor == None || morphName == ""
        return
    endif

    NiOverride.ClearBodyMorph(akActor, morphName, FBKey())
    NiOverride.ApplyMorphs(akActor)
    NiOverride.UpdateModelWeight(akActor)
EndFunction