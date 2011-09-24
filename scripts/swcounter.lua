-- sliding window counter 

local function new(windowsize) 
  local  object = {}
  if (windowsize == nil) then
     windowsize = "16"
  end
  object.size   = tonumber(windowsize)
  object.index  = 0
  object.values = {}
  local count   = 0
  for count = 1,object.size do 
	object.values[count] = 0
  end
  return object
end

local function add(object, value) 
   object.index = object.index+1
   if (object.index > object.size) then 
       object.index = 1 
   end 
   object.values[object.index] = tonumber (value) 
end

local function getlast(object)
   if (object.index ~= 0) then
       return object.values[object.index]	
   end
   return 0
end

local function getmin(object)
   local count = 0
   local min   = math.huge
   for count = 1,object.size do
       if (object.values[count] < min) then
          min = object.values[count]
       end 
   end
   return min
end

local function getmax(object)
   local count = 0
   local max   = 0
   for count = 1,object.size do
       if (object.values[count] > max) then
          max = object.values[count]
       end 
   end
   return max
end

local function getavg(object)
   local count = 0
   local total   = 0
   for count = 1, object.size do
        total = total + object.values[count]
   end
   return (total/object.size)
end


local function handleNEW(command, originalKey, cacheKey, size)
       executeNew(command, originalKey, "swcounter", cacheKey, 
           function(s)
              return new(s)
           end,
           size)
end

local function handleADD(command, originalKey, cacheKey, value)
       executeReadWrite(command, originalKey, "swcounter", cacheKey, 
           function(o, v)
              add(o, v)
              return "SUCCESS"
           end,
       value)
end


local function handleGETLAST(command, originalKey, cacheKey)
    executeReadOnly(command, originalKey, "swcounter", cacheKey, 
           function(o)
              return getlast(o)
           end
           )
end
           
local function handleGETMIN(command, originalKey, cacheKey)
    executeReadOnly(command, originalKey, "swcounter", cacheKey, 
           function(o)
              return getmin(o)
           end
           ) 
end
           
local function handleGETMAX(command, originalKey, cacheKey)
    executeReadOnly(command, originalKey, "swcounter", cacheKey, 
           function(o)
              return getmax(o)
           end
           )
end
           
local function handleGETAVG(command, originalKey, cacheKey)
    executeReadOnly(command, originalKey, "swcounter", cacheKey, 
           function(o)
              return getavg(o)
           end
           )
end           


swcounter = {
 new     = handleNEW,
 add     = handleADD, 
 getlast = handleGETLAST, 
 getmin  = handleGETMIN, 
 getmax  = handleGETMAX, 
 getavg  = handleGETAVG
 }
 
return swcounter

