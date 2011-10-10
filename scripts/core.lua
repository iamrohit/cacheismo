
local function handleGET(command) 
     local hashMap   = getHashMap()   
     local keyCount = command:hasMultipleKeys();		
     if ( keyCount > 0) then 
	local keys = command:getMultipleKeys()
	for k,v in pairs(keys) do
	    local cacheItem = hashMap:get(v)
	    if (cacheItem ~= nil) then
		command:writeCacheItem(cacheItem); 
		cacheItem:delete()
	    end 
	end 
     else 
        local cacheItem = hashMap:get(command:getKey())
	if (cacheItem ~= nil) then
            command:writeCacheItem(cacheItem); 
            cacheItem:delete()
        end 
     end 
     command:writeString("END\r\n")	
     return 0
end

local function handleSET(command) 
  local hashMap   = getHashMap()   
  local cacheItem = hashMap:get(command:getKey())
  if (cacheItem ~= nil) then 
      hashMap:delete(command:getKey())
      cacheItem:delete()
      cacheItem = nil
  end 
  cacheItem = command:newCacheItem()
  if (cacheItem ~= nil) then 
      hashMap:put(cacheItem)
      command:writeString("STORED\r\n")
  else 
     command:writeString("SERVER_ERROR Not Enough Memory\r\n")
  end 	
  return 0
end


local function handleADD(command) 
  local cacheItem = getHashMap():get(command:getKey())
  if (cacheItem ~= nil) then 
      command:writeString("NOT_STORED\r\n")
  else 
      cacheItem = command:newCacheItem()
      if (cacheItem ~= nil) then 
      	   getHashMap():put(cacheItem)
           command:writeString("STORED\r\n")
      else 
     	   command:writeString("SERVER_ERROR Not Enough Memory\r\n")
      end 	
  end 
  return 0
end

local function handleDELETE(command) 
  local cacheItem = getHashMap():get(command:getKey())
  if (cacheItem ~= nil) then 
      getHashMap():delete(command:getKey())
      cacheItem:delete()
      command:writeString("DELETED\r\n")
  else 
      command:writeString("NOT_FOUND\r\n")
  end
  return 0
end

local function handleFLUSH_ALL(command) 
     -- passing 64GB - max possible size of cache 
     getHashMap():deleteLRU(64 * 1024 * 1024 * 1024)
     command:writeString("OK\r\n")
     return 0
end

local function handleVERSION(command) 
     command:writeString("VERSION 1.4.2\r\n")
     return 0
end

local function handleVERBOSITY(command)
     -- we store the argument to verbosity in flags field
     setLogLevel(command:getFlags())
     command:writeString("OK\r\n")
     return 0
end

local function handleQUIT(command) 
    return -1
end

local function handlePREPEND(command)
    local hashMap   = getHashMap() 
    local cacheItem = hashMap:get(command:getKey())
    if (cacheItem ~= nil) then 
        local prependData = command:getData()
        local currentData = cacheItem:getData()
        cacheItem:delete()
        cacheItem = nil
        hashMap:delete(commad:getKey())
        local newData = prependData..currentData
        command:setData(newData)
        cacheItem = command:newCacheItem()
        hashMap:put(cacheItem)
        command:writeString("STORED\r\n")
    else 
    	command:writeString("NOT_STORED\r\n")
    end 
    return 0
end


local function handleAPPEND(command) 
    local hashMap   = getHashMap() 
    local cacheItem = hashMap:get(command:getKey())
    if (cacheItem ~= nil) then 
        local appendData = command:getData()
        local currentData = cacheItem:getData()
        cacheItem:delete()
        cacheItem = nil
        hashMap:delete(commad:getKey())
        local newData = currentData..appendData
        command:setData(newData)
        cacheItem = command:newCacheItem()
        hashMap:put(cacheItem)
        command:writeString("STORED\r\n")
    else 
    	command:writeString("NOT_STORED\r\n")
    end 
    return 0
end

local function handleREPLACE(command) 
    local hashMap   = getHashMap() 
    local cacheItem = hashMap:get(command:getKey())
    if (cacheItem ~= nil) then 
        local newData     = command:getData()
        local currentData = cacheItem:getData()
        cacheItem:delete()
        cacheItem = nil
        hashMap:delete(commad:getKey())
        command:setData(newData)
        cacheItem = command:newCacheItem()
        hashMap:put(cacheItem)
        command:writeString("STORED\r\n")
    else 
    	command:writeString("NOT_STORED\r\n")
    end 
    return 0
end

core = {
    get       = handleGET,
    set       = handleSET, 
    add       = handleADD,
    delete    = handleDELETE,
    flush_all = handleFLUSH_ALL,
    version   = handleVERSION,	
    quit      = handleQUIT,
    prepend   = handlePREPEND,
    append    = handleAPPEND,
    replace   = handleREPLACE
}


