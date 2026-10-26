-- updateEditor of Angry Birds 1.6.2 (data/scripts/gamelogic.lua). Angry Birds 1.6.3.1 ships the same editor with this one function emptied,
-- so the engine runs this copy when a game's own updateEditor is only a stub (script_host.cpp, EnsureClassicEditor).
-- Everything it calls (cursor, selectedObjects, createObject, saveLevel ...) is defined by the game's own scripts.
function updateEditor(dt, time)
-- editor can be preprocessed out by defining "STRIP_EDITOR"
--#ifndef STRIP_EDITOR

	if oldZoomLevel ~= zoomLevel then
		worldScale = worldScale + zoomLevel - oldZoomLevel
		setWorldScale(worldScale)
		oldZoomLevel = zoomLevel
	end	

	updateScale()
	cursorPhysics.x, cursorPhysics.y = screenToPhysicsTransform(cursor.x, cursor.y)
	cursorWorld.x, cursorWorld.y = screenToWorldTransform(cursor.x, cursor.y)

	--print("cp.x_ " .. cursorPhysics.x .. " - cp.y: " .. cursorPhysics.y .. "\n")
	--print("cw.x_ " .. cursorWorld.x .. " - cw.y: " .. cursorWorld.y .. "\n")

	-- Handle input
	if keyPressed["ESCAPE"] or keyPressed["F1"] then
		setGameMode(updateMenu)
		setPhysicsEnabled(false)
		physicsEnabled = false
		setActiveMenuPage(levelSelectionEdit[currentThemeNumber])
	end

	if keyHold["CONTROL"] and keyPressed["S"] then
		checkDirectories()
		saveLevel(levelFolder .. levelName)
		saveLevel(levelFolder .. "temp/" .. levelName .. ".temp")
		levelSaved = true
	end

	if not keyHold["CONTROL"] and keyHold["SHIFT"] and keyPressed["C"] then
		if objects.castleCameraData == nil then
			objects.castleCameraData = {}
		end
		
		objects.castleCameraData.version = "0.02"
		objects.castleCameraData[deviceModel] = { px = screen.x,
												  py = screen.y,
												  sx = worldScale,
												  sy = worldScale,
												  screenWidth = screenWidth,
												  screenHeight = screenHeight }
		levelSaved = false
	end

	if not keyHold["CONTROL"] and keyHold["SHIFT"] and keyPressed["B"] then
		if objects.birdCameraData == nil then
			objects.birdCameraData = {}
		end
		
		objects.birdCameraData.version = "0.02"
		objects.birdCameraData[deviceModel] = { px = screen.x,
												py = screen.y,
												sx = worldScale,
												sy = worldScale,
												screenWidth = screenWidth,
												screenHeight = screenHeight }
		levelSaved = false
	end
	
	--debug camera
	
	if keyHold["CONTROL"] and keyHold["SHIFT"] and keyPressed["SPACE"] then 
		 
		if g_debugCameras == nil then
			g_debugCameras = true
		else 
			g_debugCameras = not g_debugCameras	
			g_cameraToDraw	= nil
		end								
		
	end
	
	if g_debugCameras and keyPressed["LBUTTON"] then								
		for i = 1, #g_camerasList, 1 do
			local t_initialX = 20
			local t_initialY = 150
			
			local t_offsetY = 50
			
			if checkTextBounds("TEXTS_BASIC", g_camerasList[i], "LEFT", "BOTTOM", t_initialX, t_initialY + (t_offsetY * (i-1)), cursor.x, cursor.y) then
				g_cameraToDraw = g_camerasList[i]
				break
			end
		end
	end

	if keyHold["SHIFT"] and keyPressed["P"] then
		setEditing(false)
		setPhysicsEnabled(false)
		local name = "temp/" .. levelName .. ".temp.playtest"
		checkDirectories()
		saveLevel(levelFolder .. name)
		loadLevelInternal(levelFolder .. name)
		startedFromEditor = true
		setGameMode(updateGame)
		levelSelectionPageNumber = currentThemeNumber
		currentThemeNumber = currentThemeIndex
	end

	if keyPressed["TAB"] then
		physicsEnabled = not physicsEnabled
		if physicsEnabled then
			checkDirectories()
			saveLevel(levelFolder .. "temp/" .. levelName .. ".temp")
		else
			loadLevelInternal(levelFolder .. "temp/" .. levelName .. ".temp")
		end
		setPhysicsEnabled(physicsEnabled)
	end

	if keyPressed["MBUTTON"] then
		if keyPressed["SHIFT"] then
			setWorldScale(1)
			worldScale = 1
		else
			setWorldScale(1)
			worldScale = 1
			screen.x = 0
			screen.y = 0
		end
	end

	-- set active theme
	if keyHold["SHIFT"] and keyPressed[blockTable.themes.settings.keyCode] then
		currentThemeIndex = currentThemeIndex + 1
		if currentThemeIndex > blockTable.themes.settings.themeAmount then
			currentThemeIndex = 1
		end

		for k0, v0 in _G.pairs(blockTable.themes) do
			if v0.index ~= nil and v0.index == currentThemeIndex then
				currentTheme = k0
				setTheme(currentTheme)
				-- replace all blocks that are theme dependent
				for k1, v1 in _G.pairs(objects.world) do
					v1def = blockTable.blocks[v1.definition]
					if v1def.theme ~= nil and v1def.theme ~= currentTheme then
						-- go through all block definitions to find the correct block to replace this one
						for k2, v2 in _G.pairs(blockTable.blocks) do
							-- if this block definition's group is the same as the one on the current block and the group indexes match then check the theme
							if v2.group == v1def.group and v2.groupIndex == v1def.groupIndex then
								-- does the theme match to the current theme requirements
								if v2.theme == nil or v2.theme == currentTheme then
									-- we found a replacement, update values accordingly
									v1.definition = k2
									v1.sprite = v2.sprite
									v1.damageSprite = v1.sprite
									setSprite(k1, v1.sprite)
									levelSaved = false
								end
							end
						end
					end
				end

				-- update the cursor object
				if objectToAdd ~= nil then
					updateCursorObjectAccordingToTheme()
				end
				-- replace level defines
				objects.theme = currentTheme

			end
		end
	end

	birdSelected = false
	if selectedObjects[1] ~= nil then
		if selectedObjects[1].controllable then
			birdSelected = true
			for i = 1, #numberKeys do
				if keyPressed[numberKeys[i]] then
					selectedObjects[1].startNumber = i
				end
			end
		end
	end


	-- create blocks from the blocks.lua definitions
	if keyHold["CONTROL"] == false and keyHold["SHIFT"] == false and birdSelected == false then
		for k, v in _G.pairs(blockTable.groups) do
			local groupDataUpdate = false
			if keyPressed[v.keyUp] then
				-- update group information
				if currentGroup == k then
					currentGroupIndex = currentGroupIndex + 1
				else
					currentGroup = k
				end
				groupDataUpdate = true
			end
			if keyPressed[v.keyDown] then
				-- update group information
				if currentGroup == k then
					currentGroupIndex = currentGroupIndex - 1
				else
					currentGroup = k
				end
				groupDataUpdate = true
			end
			-- find the next object to add based on group information
			if groupDataUpdate then
				if currentGroupIndex > blockTable.groups[currentGroup].lastIndex then
					currentGroupIndex = blockTable.groups[currentGroup].firstIndex
				end
				if currentGroupIndex < blockTable.groups[currentGroup].firstIndex then
					currentGroupIndex = blockTable.groups[currentGroup].lastIndex
				end

				--print("Searching for group " .. currentGroup .. " " .. currentGroupIndex .. "\n")
				updateCursorObjectAccordingToTheme()
			end
		end
	end

	if keyPressed["RBUTTON"] then
		if copiedObjects ~= nil then
			copiedObjects = nil
		end
		
		if objectToAdd ~= nil then
			objectToAdd = nil
		else
			if not keyHold["SHIFT"] and not keyHold["ALT"] then
				selectedObjects = { }
			end
			selectedObjectPos.x = 0
			selectedObjectPos.y = 0

			draggingStartPosWorld.x = cursorWorld.x
			draggingStartPosWorld.y = cursorWorld.y

			--find object that was clicked
			for k, v in _G.pairs(objects.world) do
				object = v
				if object.type == "polygon" then
					x = object.x
					y = object.y
					--print(object.name .. " ")
					if checkPolygonObjectBounds(x, y, object.width, object.height, object.angle, getObjectDefinition(k).vertices, cursorPhysics.x, cursorPhysics.y) then
						addObjectToSelection(object, true)
					end
				end
				if object.type == "box" then
					x = object.x
					y = object.y
					--print(object.name .. " ")
					if checkObjectBounds(x, y, object.width, object.height, object.angle, cursorPhysics.x, cursorPhysics.y) then
						addObjectToSelection(object, true)
					end
				end
				if object.type == "circle" then
					if distance(object.x, object.y, cursorPhysics.x, cursorPhysics.y) < object.radius then
						addObjectToSelection(object, true)
					end
				end
			end
		end
	end

	if keyHold["RBUTTON"] then
		selectionRectActive = true
	end

	-- this is here so that the dragging position is not set on this frame if LBUTTON is released
	if keyReleased["RBUTTON"] then
		if objectToAdd ~= nil and selectedObjects == nil or #selectedObjects < 1 then
			selectedObjects = getObjectsInsideRect(draggingStartPosWorld.x, draggingStartPosWorld.y, cursorWorld.x, cursorWorld.y)
		else
			if keyHold["SHIFT"] then
				local tempObjects = getObjectsInsideRect(draggingStartPosWorld.x, draggingStartPosWorld.y, cursorWorld.x, cursorWorld.y)
				for k, v in _G.pairs(tempObjects) do
					addObjectToSelection(object, false)
				end
			end
			if keyHold["ALT"] then
				local tempObjects = getObjectsInsideRect(draggingStartPosWorld.x, draggingStartPosWorld.y, cursorWorld.x, cursorWorld.y)
				for k, v in _G.pairs(tempObjects) do
					for soi = 1, #selectedObjects do
						if selectedObjects[soi] == v then
							_G.table.remove(selectedObjects, soi)
							soi = #selectedObjects
						end
					end
				end				
			end
		end
		selectionRectActive = false
	end

	if keyPressed["LBUTTON"] then
		oldCursor.x = cursor.x
		oldCursor.y = cursor.y
		if not keyHold["SPACE"] then
			if objectToAdd ~= nil then
				local name = createObject(blockTable, objectToAdd, nil, cursorPhysics.x, cursorPhysics.y)
				setRotation(name, objectToAddAngle)
				selectedObjects = {}
				_G.table.insert(selectedObjects, objects.world[name])
				levelSaved = false
			end
			if copiedObjects ~= nil then
				for k, v in _G.pairs(copiedObjects) do
					local name = createObject(blockTable, v.definition, nil, cursorPhysics.x + v.x, cursorPhysics.y + v.y)
					setRotation(name, v.angle)
				end
			end
		end
	end

	if keyHold["LBUTTON"] then
		if keyHold["SPACE"] then
			screen.x = screen.x - (cursorWorld.x - draggingStartPosWorld.x) * 0.5
			screen.y = screen.y - (cursorWorld.y - draggingStartPosWorld.y) * 0.5
		else
			if objectToAdd ~= nil then
				-- do not allow object selection if object to add is active
			elseif selectedObjects ~= nil and #selectedObjects > 0 then
				for k, v in _G.pairs(selectedObjects) do
					object = v
					x, y = worldToPhysicsTransform(cursor.x - oldCursor.x, cursor.y - oldCursor.y)
					x = x / worldScale
					y = y / worldScale
					setSleeping(object.name, false)
					if keyHold["CONTROL"] then
						setRotation(object.name, object.angle + (cursor.x - oldCursor.x)/180 * _G.math.pi )
					else
						setPosition(object.name, x + object.x, y + object.y)
					end
				end
				levelSaved = false
			end
		end
	end

	if not keyHold["LBUTTON"] and not keyHold["RBUTTON"] then
		draggingStartPosWorld.x = cursorWorld.x
		draggingStartPosWorld.y = cursorWorld.y
	end

	-- handle key input
	if selectedObjects ~= nil and #selectedObjects > 0 then
		local moveAmount = 1
		if keyHold["SHIFT"] then
			moveAmount = 10
		end	
		if keyHold["SHIFT"] and keyHold["CONTROL"] then
			moveAmount = 100
		end
		if not keyHold["SHIFT"] and keyHold["CONTROL"] then
			-- remove movement if only control pressed
			moveAmount = 0
		end
		
		if keyReleased["DELETE"] then
			for k, v in _G.pairs(selectedObjects) do
				local name = v.name
				objects.world[name] = nil
				removeObject(name)
			end
			selectedObjects = {}
			levelSaved = false
		end
		
		local moveKeyDown = false

		if keyHold["LEFT"] or keyHold["RIGHT"] or keyHold["UP"] or keyHold["DOWN"] then
			moveKeyDown = true
		end
		
		if blockMoveTimer == 0 or blockMoveTimer > 0.3 then
			if keyHold["LEFT"] then
				setPositions(-moveAmount, 0)
			end
			if keyHold["RIGHT"] then
				setPositions(moveAmount, 0)
			end
			if keyHold["UP"] then
				setPositions(0, -moveAmount)
			end
			if keyHold["DOWN"] then
				setPositions(0, moveAmount)
			end
		end
		
		if moveKeyDown then
			blockMoveTimer = blockMoveTimer + dt
		else
			blockMoveTimer = 0
		end
	end

	if keyHold["SHIFT"] then
		if keyPressed["R"] then
			if copiedObjects ~= nil then
				local px, py, w, h = getObjectListBounds(copiedObjects)
				for k, v in _G.pairs(copiedObjects) do
					local angle = _G.math.pi/4
					v.angle = v.angle + angle
					
					-- move to origin
					local cx = v.x - px
					local cy = v.y - py
					
					-- rotate around origin
					local tcx = cx * _G.math.cos(angle) - cy * _G.math.sin(angle)
					local tcy = cx * _G.math.sin(angle) + cy * _G.math.cos(angle)
	
					-- move back
					v.x = tcx + px
					v.y = tcy + py
					--print("Rotating copied object: " .. k .. "\n")
				end
			else
				if objectToAdd ~= nil then
					objectToAddAngle = objectToAddAngle + _G.math.pi/4
				else
					if selectedObjects ~= nil and #selectedObjects > 0 then
						for k, v in _G.pairs(selectedObjects) do
							setRotation(v.name, v.angle + _G.math.pi/4)
						end
						levelSaved = false
					end
				end
			end
		end
		
		if keyPressed["J"] then
			if #selectedObjects == 2 then
				createJoint(selectedObjects[1].name .. selectedObjects[2].name, selectedObjects[1].name, selectedObjects[2].name, 1, 2, 0, 0, 0, 0)
				levelSaved = false
			end
		end
	end
		
	if not keyHold["SHIFT"] and keyHold["CONTROL"] then		
		if keyPressed["LEFT"] or keyPressed["RIGHT"] then
			if selectedObjects ~= nil and #selectedObjects > 0 then
				local px, py, w, h = getObjectListBounds(selectedObjects)
								
				for k, v in _G.pairs(selectedObjects) do
					setPosition(v.name, px + (px - v.x), v.y)
				end
				levelSaved = false
			end			
		end
	
		if keyPressed["UP"] or keyPressed["DOWN"] then
			if selectedObjects ~= nil and #selectedObjects > 0 then
				local px, py, w, h = getObjectListBounds(selectedObjects)
								
				for k, v in _G.pairs(selectedObjects) do
					setPosition(v.name, v.x, py + (py - v.y))
				end
				levelSaved = false
			end
		end
	end
	
	if keyHold["SHIFT"] and keyHold["CONTROL"] and (keyPressed["B"] or keyPressed["HOME"]) then
		if objects.birdCameraData and objects.birdCameraData[deviceModel] ~= nil then
			screen.x = objects.birdCameraData[deviceModel].px
			screen.y = objects.birdCameraData[deviceModel].py
			worldScale = objects.birdCameraData[deviceModel].sx
			setWorldScale(worldScale)
			oldScale = worldScale
		end
	end
	
	if keyHold["SHIFT"] and keyHold["CONTROL"] and (keyPressed["C"] or keyPressed["END"]) then
		if objects.castleCameraData ~= nil and objects.castleCameraData[deviceModel]then
			screen.x = objects.castleCameraData[deviceModel].px
			screen.y = objects.castleCameraData[deviceModel].py
			worldScale = objects.castleCameraData[deviceModel].sx
			setWorldScale(worldScale)
			oldScale = worldScale
		end
	end
	
	if not keyHold["SHIFT"] and not keyHold["CONTROL"] and keyPressed["END"] then
		if objects.doNotWaitForMovingObjects ~= nil then
			objects.doNotWaitForMovingObjects = nil
		else
			objects.doNotWaitForMovingObjects = true
		end
	end
	
	if not keyHold["SHIFT"] and keyHold["CONTROL"] and keyPressed["C"] then
		copiedObjects = {}
		local x, y, w, h = getObjectListBounds(selectedObjects)
		for k, v in _G.pairs(selectedObjects) do
			--print("Adding to copied objects: " .. v.name .. "\n")
			copiedObjects[v.name] = { name = v.name, definition = v.definition, x = v.x - x, y = v.y - y, angle = v.angle }
			if v.width == nil then
				copiedObjects[v.name].width = v.radius
				copiedObjects[v.name].height = v.radius
			else
				copiedObjects[v.name].width = v.width
				copiedObjects[v.name].height = v.height
			end
		end
		selectedObjects = {}
	end
	
	defaultCamera(dt)

	oldCursor.x = cursor.x
	oldCursor.y = cursor.y	
	
	drawGame()
--#endif
end
