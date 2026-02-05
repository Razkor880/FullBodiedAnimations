Scriptname NiOverride Hidden

; Compile-time stub for FBMorphBridge.
; Do NOT ship this .pex. Runtime uses RaceMenu's NiOverride.pex.

Function SetBodyMorph(ObjectReference ref, String morphName, String keyName, Float value) native global
Function ClearBodyMorph(ObjectReference ref, String morphName, String keyName) native global
Function UpdateModelWeight(ObjectReference ref) native global

; If you’re calling ApplyMorphs in your bridge, stub it too.
Function ApplyMorphs(ObjectReference ref) native global
