# Interlink-Message-Forwarder
This is a prototype Tier1-3 implement the files in Global...

Concept is to provide callers a simple TCP interface to send message between nodes that provide a particular function. To be used within datacenter on a 10 Gbit link

The goal of this is to provide basically a "high speed message stream" meaning without the reliance of a broker a processes can communicate interhost on a common port / queue name
This does not "hold messages" or "replay messages" at the most it will keep a count if the user desires when hosts go down and traffic shifts between nodes.

This is not meant to replace a broker like ("AMPS", "KAFKA", "ACTIVE MQ" etc..) this is to be used for time sensitive data with extremely fast SLAs, with 0 external dependence (like a broker)

if your requirements need messages to be replayed at a later date or for them to be guaranteed (meaning replaying them to another node if a message is lost in transit) , 
it is possible since the application can keep track, but that will be managed by the developer. And even then i would strongly reccomend looking at the solutions I listed above

Will upload a formal design document at a later date.

Feel free to email questions to slomanshield@gmail.com
