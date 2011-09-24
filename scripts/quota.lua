-- simple quota implementation 
local quotaTypesMap = { month = 1, day = 1, hour = 1 }

local function findExpiresAt(quotaType)
       local current = os.date("*t")
       if (quotaType == "month") then
           current.month = current.month + 1
           current.day   = 1
           current.hour  = 0
           current.min   = 0
           current.sec   = 1
       elseif (quotaType == "day") then
           current.day   = current.day + 1
           current.hour  = 0
           current.min   = 0
           current.sec   = 1
       else 
           current.hour = current.hour+1
           current.min   = 0
           current.sec   = 1
       end 
    return os.time(current)
end

local function new(limit, quotaType) 
  if (quotaTypesMap[quotaType] == nil) then
      return nil
  end

  local  object    = {}
  object.limit     = tonumber(limit)
  object.type      = quotaType
  object.count     = 0
  object.expiresAt = findExpiresAt(quotaType)
  return object
end

local function addandcheck(object, value)
   local toUseValue = 1 
   if (value ~= nil) then
      toUseValue = tonumber(value)
   end
   if (os.time() > object.expiresAt) then
       object.count = 0;
       object.expiresAt = findExpiresAt(object.type) 
   end
   if ((object.count + toUseValue) > object.limit) then
       return -1
   else 
       object.count = object.count + toUseValue
       return 0
   end
end

local function reset(object) 
   if (os.time() > object.expiresAt) then
       object.count = 0
       object.expiresAt = findExpiresAt(object.type) 
   end
   object.count = 0
   return 0	
end


local function handleNEW(command, originalKey, cacheKey, limit, quotaType) 
    executeNew(command, originalKey, "quota", cacheKey, 
              function (l, q)
                  return new(l, q) 
              end,
              limit, 
              quotaType)

end

local function handleADDANDCHECK(command, originalKey, cacheKey, value) 
    executeReadWrite(command, originalKey, "quota", cacheKey, 
                    function (o, v)
                        if (addandcheck(o, v) == 0) then 
                           return "QUOTA_OK"
                        else 
                           return "QUOTA_EXCEEDED"
                        end  
                    end, 
                    value)
end

local function handleRESET(command, originalKey, cacheKey) 
    executeReadWrite(command, originalKey, "quota", cacheKey, 
                    function (o)
                         reset(o)
                         return "QUOTA_OK"
                    end, 
                    value)
end

quota = {
   new         = handleNEW, 
   addandcheck = handleADDANDCHECK,
   reset       = handleRESET,
}

return quota

