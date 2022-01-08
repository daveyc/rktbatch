# RKTBATCH

`RKTBATCH` is a batch utility for running shell scripts, or any z/OS UNIX command. It differs from IBM utilities such as `BPXBATCH` in that `STDIN` can contain shell script source code. 
Another important different is that `RKTBATCH` uses local spawn which runs the script in the same address space so DD data sets allocations can be used in scripts eliminating the need for data at rest
by copying data set to the file system. 

## Building

Build using `make`. To install to an MVS load library which will default to `$USER.LOAD(RKTBATCH)` run `make install`.

## Running

This example shows how to create a Jira using Rocket ported tools and integration to the MVS file system using `DD:JIRADATA`.
```
//NEWJIRA  JOB  NOTIFY=&SYSUID                                 
//RKTBATCH EXEC PGM=RKTBATCH,PARM='/ /bin/sh -L'               
//STDERR   DD  SYSOUT=*                                        
//STDOUT   DD  SYSOUT=*                                        
//STDIN    DD  *                                               
cat //DD:JIRADATA | \               # read MVS data set        
iconv -t ISO8859-1 -f IBM-1047 | \  # convert to ASII          
curl --request POST \               # POST API call to Jira    
  --url 'https://<your-jira-cloud-instance>/rest/api/3/issue' \
  --header 'Authorization: Basic YourEncodedStringHere' \      
  --header 'Accept: application/json' \                        
  --header 'Content-Type: application/json' \                  
  --data @-                                                    
/*                                                             
//JIRADATA DD  *                                               
"fields": {                                                    
    "summary": "Ported Tools are awesome!",                    
    "issuetype": {                                             
        "id": "10009"                                          
    },                                                         
    "project": {                                               
        "key": "PORT"                                          
    },                                                         
    "description": {                                           
        "type": "doc",                                         
        "version": 1,                                          
        "content": [                                           
            {                                                  
                "type": "paragraph",                           
                "content": [                                   
                    {                                          
                        "text": "This is the description.",    
                        "type": "text"                         
                    }                                          
                ]                                              
            }                                                  
        ]                                                      
    }                                                          
}                                                              
/*                                                             
```
## Console commands

`RKTBATCH` supports the MVS STOP command which is useful if the utility is running a started task. No other batch shell utility such as `COZBATCH` support this feature. 

```
P MYJOB
```
