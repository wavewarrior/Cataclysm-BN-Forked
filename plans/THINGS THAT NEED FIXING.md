THINGS THAT NEED FIXING

- the soundwavefront shader effect is too small. the rings need to be a bit larger, add in a knob in the debug UI panel so we can adjust params
- HUD particle effects are not visible at all. did you take hi-dpi into account same as other particles like the one in main menu (take as example)
- weird lighting flash (like whole lightmap is offset) for one frame when crossing submap boundary
- cant light fires using lighters or other items that should allow this. upon activation it states lost item trying to light fire wiht

- game crashed upon trying to drive vehicle (likely Box2D vehicle-physics position desync — see plans/mouse-interactivity-followup-bugs.md §1 for confirmed failing tests and next steps)
- box2D hitboxes debug overlay not working. 
- sound placement through debug UI not working. something severly wrong with debug UI focus. click handling sometimes stops working need to close and reopen to regain focus.
- HUD UI log does not scroll properly, panel should not extend to bottom off screen makes unclear what last message was. rolling text behaviour incorrect. panel should end with clear separation divider element so we know when panel ends and next begins.
- Piper TTS is not working or inactive? when conversing with NPC i expect it to trigger and work.
- Normal map generation still needs to be implemented properly as stated in the GRAVEYARD_KEEPER plan in the plans/done folder.
- Windows should act as light portals as done in many other games, to let more daylight seep in in an indoor scenario. also add shader effect for godrays entering through windows with dust particles floating in it. 
- the lean and stretch effect should only be applied to trees. it feels weird on walls and such.
- firing/aiming window is fullscreen so cant see where aiming. (right-click-for-aiming is now fixed as of the mouse interactivity work — right-click while armed enters fire/throw targeting; if the fullscreen-obstruction complaint persists, it's now purely a UI layout issue, not a dead input path)
- 
-  