--if os.getenv('DEBUG_MODE') then require "debugger"() end

local class = require("org.xboot.lang.class")
local table = require("org.xboot.lang.table")
local event = require("org.xboot.event.event")
local event_dispatcher = require("org.xboot.event.event_dispatcher")
local display_object = require("org.xboot.display.display_object")
local event_peek = require("org.xboot.event.event_peek")
local printr = require("org.xboot.util.printr")


local top  = display_object:new("top")
local obj1 = display_object:new("obj1")
local obj2 = display_object:new("obj2")
local obj3 = display_object:new("obj3")

top:add_child(obj1)
top:add_child(obj2)
top:add_child(obj3)

local function obj1_on_mouse_down(e)
    print("DOWN " .. " [" .. e.ext.x .. "," .. e.ext.y .. "]")
end

local function obj1_on_mouse_up(e)
	print("UP   " .. " [" .. e.ext.x .. "," .. e.ext.y .. "]")
end

local function obj1_on_mouse_move(e)
	print("MOVE " .. " [" .. e.ext.x .. "," .. e.ext.y .. "]")
end

obj1:add_event_listener(event.MOUSE_DOWN, obj1_on_mouse_down)
obj1:add_event_listener(event.MOUSE_UP, obj1_on_mouse_up)
obj1:add_event_listener(event.MOUSE_MOVE, obj1_on_mouse_move)

print("=======main test start=================")

while true do
	local ep = event_peek.peek()
	if ep ~= nil then
		local e = event:new(ep.type)
		e.ext = ep
		
		obj1:dispatch_event(e)
	end
end

print("=======main test end=================")
