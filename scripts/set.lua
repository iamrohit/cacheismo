
local function new() 
  local  set = {}
  return set
end

local function put(set, key) 
  set[key]=1   
end

local function exists(set, key)
  if (set[key] == 1) then
     return 1
  end
  return 0
end

local function delete(set, key)
     set[key] = nil
  return 
end 

local function count(set)
  return #set
end

local function getall(set)  
    local result = ""
	for k,v in pairs(set) do
           result = result .. k .. "\r\n" 
    end
    return result
end


function union(set, set2)  
    local newset = {}
	for k,v in pairs(set) do
              newset[k] = 1
        end
	for k,v in pairs(set2) do
             newset[k] = 1
    end
   return getall(newset)
end

function intersection(set, set2)  
        local toIter
        local toLookup
        local newSet = {}

        if (#set > #set2) then 
            toIter   = set2
            toLookup = set
        else 
            toIter   = set
            toLookup = set2
        end

	for k,v in pairs(toIter) do
           if (toLookup[k] == 1) then 
               newSet[k] = 1
           end
        end
   return getall(newSet)
end


local function handleNEW(command, originalKey, cacheKey)
       executeNew(command, originalKey, "set", cacheKey, 
           function()
              return new()
           end
           )
end

local function handleEXISTS(command, originalKey, cacheKey, objectKey)
       executeReadOnly(command, originalKey, "set", cacheKey, 
           function(o, k)
              if (exists(o, k) == 1) then 
                  return "EXISTS"
              else
                  return "NOT_FOUND" 
              end
           end,
       objectKey)
end

local function handlePUT(command, originalKey, cacheKey, objectKey)
       executeReadWrite(command, originalKey, "set", cacheKey, 
           function(o, k)
              put(o,k)
              return "SUCCESS"
           end,
       objectKey)

end


local function handleCOUNT(command, originalKey, cacheKey)
    executeReadOnly(command, originalKey, "set", cacheKey, 
           function(o)
              return count(o)
           end)
end

local function handleDELETE(command, originalKey, cacheKey, objectKey)
       executeReadWrite(command, originalKey, "set", cacheKey, 
           function(o, k)
              delete(o,k)
              return "SUCCESS"
           end,
       objectKey)

end

local function handleGETALL(command, originalKey, cacheKey)
    executeReadOnly(command, originalKey, "set", cacheKey, 
           function(o)
              return getall(o)
           end)
end

local function handleUNION(command, originalKey, cacheKey1, cacheKey2)
    local hashMap    = getHashMap()
    local cacheItem1 = hashMap:get("set$"..cacheKey1)
    local cacheItem2 = hashMap:get("set$"..cacheKey2)
    
    if (cacheItem1 ~= nil and cacheItem2 ~= nil) then 
        local sobject1, sobject2
        local object1, object2
        sobject1 = cacheItem1:getData()
        cacheItem1:delete()
        object1  = table:unmarshal(sobject1)
        sobject2 = cacheItem2:getData()
        cacheItem2:delete()
        object2  = table:unmarshal(sobject2)
        
        local result = union(object1, object2)
        writeStringAsValue(command, originalKey, result)
        return 
    end 
    writeStringAsValue(command, originalKey, "ERROR_CACHE_MISS")
end

local function handleINTERSECTION(command, originalKey, cacheKey1, objectKey2)
    local hashMap    = getHashMap()
    local cacheItem1 = hashMap:get("set$"..cacheKey1)
    local cacheItem2 = hashMap:get("set$"..cacheKey2)
    
    if (cacheItem1 ~= nil and cacheItem2 ~= nil) then 
        local sobject1, sobject2
        local object1, object2
        sobject1 = cacheItem1:getData()
        cacheItem1:delete()
        object1  = table:unmarshal(sobject1)
        sobject2 = cacheItem2:getData()
        cacheItem2:delete()
        object2  = table:unmarshal(sobject2)
    
        local result = intersection(object1, object2)
        writeStringAsValue(command, originalKey, result)
        return 
    end 
    writeStringAsValue(command, originalKey, "ERROR_CACHE_MISS")
end
     

set = {
    new          = handleNEW,
    exist        = handleEXISTS,
    put          = handlePUT,
    count        = handleCOUNT,
    delete       = handleDELETE,
    getall       = handleGETALL,
    union        = handleUNION,
    intersection = handleINTERSECTION     
}

return set

