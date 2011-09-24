 
-- main function called by c when it recieves a new command
-- this is used when virtual key support is not enabled
function mainNormal(command)
    local cmdType  = command:getCommand()
    local executer = core[cmdType]
        
	if (executer == nil) then
        return -1
    end
    return executer(command)
end
 
-- main function called by c when it recieves a new command
-- this is used when virtual key support is enabled
function mainVirtualKey(command)
    local cmdType  = command:getCommand()
    local executer = core[cmdType]
        
	if (executer == nil) then
        return -1;
    end

	if (cmdType ~= "get") then
       executer(command)
       return 0
    end

    -- handling get command 
    local key = command:getKey()
    if (key ~= nil) then       		
        local func, args =  getVirtualKeyHandler(key)
        if (func ~= nil) then 
            func(command, key, unpack(args))
        else 
            handleGET(command, key)
        end
        command:writeString("END\r\n")	
    else
        local keys = command:getMultipleKeys()
	    for k,v in pairs(keys) do
		    local func, args =  getVirtualKeyHandler(v)
		    if (func ~= nil) then 
			    func(command, v, unpack(args))
            else 
                handleGET(command, v)
		    end
        end	
        command:writeString("END\r\n") 
    end
    return 0 	
end


function handleGET(command, key) 
    local cacheItem = getHashMap():get(key)
    if (cacheItem ~= nil) then
	    command:writeCacheItem(cacheItem)
	    cacheItem:delete()
    end 	
end

-- helper function to write arbitrary value as if 
-- it is result of a get operation
function writeStringAsValue(command, key, value) 
    command:writeString(string.format("VALUE %s 0 %d\r\n", key, string.len(value)))
    command:writeString(value)
    command:writeString("\r\n")
end

-- finds the object type and the function to invoke 
-- and other arguments from the key
function getVirtualKeyHandler(key) 
        local args = split(key) 
        if ((args ~= nil) and (#args >= 2)) then
	    local t = args[1]
        local f = args[2]
	    table.remove(args, 1)
	    table.remove(args, 1)
	    if (_G[t] ~= nil and _G[t][f] ~= nil) then
               return _G[t][f], args 
            end 		
        end
        return nil, nil
end

function split(str) 
    local result = {}
	local s = 0
	local e = 0
	local i = 1
	local length = string.len(str)

	while (i <= length) do 
	     local s, e = string.find (str, ":", i, true)
	     if (s ~= nil) then 
            table.insert(result, string.sub(str, i, s-1))
            i = s + 1 
	     else
	        table.insert(result, string.sub(str, i, length))
		    break
	     end 
	end 
    return result
end

-- helper function for read only operations on lua tables stored in the cache 
function executeReadOnly(command, originalKey, objectType, cacheKey, func, ...) 
    local hashMap   = getHashMap()
    local cacheItem = hashMap:get(objectType.."$"..cacheKey)
    if (cacheItem ~= nil) then 
        local sobject = cacheItem:getData()
        cacheItem:delete()
        
        local object  = table:unmarshal(sobject)
        local result  = func(object, unpack(arg))
        writeStringAsValue(command, originalKey, result)
        return 
    end 
    writeStringAsValue(command, originalKey, "ERROR_CACHE_MISS")
end

-- helper function for read/write operations on lua tables stored in the cache 
-- orginal object is deleted and a new object is created with the same key with 
-- latest data values 
function executeReadWrite(command, originalKey, objectType, cacheKey, func, ...) 
    local hashMap   = getHashMap()
    local cacheItem = hashMap:get(objectType.."$"..cacheKey)
    if (cacheItem ~= nil) then 
        local sobject = cacheItem:getData()
        cacheItem:delete()
      
        local object  = table:unmarshal(sobject)
        local result  = func(object, unpack(arg))    
        sobject = table.marshal(object)
        hashMap:delete(objectType.."$"..cacheKey)
        command:setKey(objectType.."$"..cacheKey)
        command:setData(sobject)
        cacheItem = command:newCacheItem()
        hashMap:put(cacheItem)
        writeStringAsValue(command, originalKey, result)
        return 
    end 
    writeStringAsValue(command, originalKey, "ERROR_CACHE_MISS")
end

-- helper function for creation of new objects based on lua tables 
-- if the key is in use, it is deleted before creating the new object
function executeNew(command, originalKey, objectType, cacheKey, func, ...) 
    local hashMap   = getHashMap()
    local cacheItem = hashMap:get(objectType.."$"..cacheKey)
    if (cacheItem ~= nil) then 
        cacheItem:delete()
        hashMap:delete(objectType.."$"..cacheKey)
        cacheItem = nil
    end
    local object = func(unpack(arg)) 
    if (object ~= nil) then 
        local sobject =  table.marshal(object)
        command:setKey(objectType.."$"..cacheKey)
        command:setData(sobject)
        cacheItem = command:newCacheItem()
        hashMap:put(cacheItem)
        writeStringAsValue(command, originalKey, "CREATED")
    else 
        writeStringAsValue(command, originalKey, "NOT_CREATED")
    end 
end    

