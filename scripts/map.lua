
local function new() 
  local  map = {}
  return map
end

local function put(map, key, value) 
  map[key]=value   
end

local function get(map, key)
  return map[key]
end

local function delete(map, key)
     local value = map[key]
     map[key] = nil
     return value	
end 

local function count(map)
  return #map
end

local function getkeys(map)  
        local result = ""
	for k,v in pairs(map) do
           result = result .. k .. "\r\n" 
        end
   return result
end

local function getvalues(map)  
        local result = ""
	for k,v in pairs(map) do
           result = result .. v .. "\r\n" 
        end
   return result
end

local function getall(map)  
        local result = ""
	for k,v in pairs(map) do
           result = result ..k.." : ".. v.. "\r\n" 
        end
   return result
end
 
local function handleNEW(command, originalKey, cacheKey)
       executeNew(command, originalKey, "map", cacheKey, 
           function()
              return new()
           end
         )
end

local function handleGET(command, originalKey, cacheKey, objectKey)
       executeReadOnly(command, originalKey, "map", cacheKey, 
           function(o, k)
              local value = get(o, k)
              if ( value ~= nil) then 
                  return k .. " : " .. value
              else
                  return "NOT_FOUND" 
              end
           end,
       objectKey)
end

local function handlePUT(command, originalKey, cacheKey, objectKey, objectValue)
       executeReadWrite(command, originalKey, "map", cacheKey, 
           function(o, k, v)
              put(o, k, v)
              return "SUCCESS"
           end,
       objectKey, objectValue)
end

local function handleCOUNT(command, originalKey, cacheKey)
       executeReadOnly(command, originalKey, "map", cacheKey, 
           function(o)
             return count(o)
           end
       )
end

local function handleDELETE(command, originalKey, cacheKey, objectKey)
       executeReadWrite(command, originalKey, "map", cacheKey, 
           function(o, k)
              delete(o,k)
              return "SUCCESS"
           end,
       objectKey)
end

local function handleGETKEYS(command, originalKey, cacheKey)
       executeReadOnly(command, originalKey, "map", cacheKey, 
           function(o)
             return getkeys(o)
           end
           )
end

local function handleGETVALUES(command, originalKey, cacheKey)
       executeReadOnly(command, originalKey, "map", cacheKey, 
           function(o, k)
              return getvalues(o)
           end
           )
end

local function handleGETALL(command, originalKey, cacheKey)
       executeReadOnly(command, originalKey, "map", cacheKey, 
           function(o)
              return getall(o)
           end
        )
end
  
  
map = {
  new       = handleNEW,
  get       = handleGET,
  put       = handlePUT,
  count     = handleCOUNT,
  delete    = handleDELETE,
  getkeys   = handleGETKEYS,
  getvalues = handleGETVALUES,
  getall    = handleGETALL
  }
  
  return map

