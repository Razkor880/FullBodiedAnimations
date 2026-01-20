Scriptname FB_DebugReloadQuest extends Quest

Event OnInit()
	Debug.Trace("[FB] Debug quest OnInit")
	Debug.Notification("[FB] Debug quest started")
	RegisterForSingleUpdate(3.0) 
EndEvent

Event OnUpdate()

EndEvent
