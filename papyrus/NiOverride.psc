Scriptname NiOverride Hidden

; Compile-time stub for FBMorphBridge.
; Do NOT ship this .pex. Runtime uses RaceMenu's NiOverride.pex.

Function SetBodyMorph(Actor akActor, String morphName, String keyName, Float value) native global
Function ClearBodyMorph(Actor akActor, String morphName, String keyName) native global
Function UpdateModelWeight(Actor akActor) native global
