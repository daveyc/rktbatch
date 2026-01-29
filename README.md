# RKTBATCH

`RKTBATCH` is a batch utility that allows a Unix command or shell script to be executed in a batch job step. Unlike less powerful IBM utilities such as `BPXBATCH`,
`RKTBATCH` is designed to simplify the integration of z/OS Unix with traditional batch jobs.


## Building

### Install dependencies
```
git submodule update --init --recursive
```

Build using `make`. To install to an MVS load library, run `make install`. By default, it installs to `$USER.LOAD(RKTBATCH)`.

## Installing

Download the `rktbatch` binary from a release to the z/OS UNIX file system and copy the file to a load data set:
```sh
cp rktbatch "//'HLQ.LOADLIB(RKTBATCH)'"
```

## Usage
```
Usage: RKTBATCH [--help] [--version] [--disable-console-commands] [--log-level VAR] [program]...

Positional arguments:
  program                     the name of the program to run. Default is the shell [nargs: 0 or more]

Optional arguments:
  -h, --help                  shows help message and exits
  -v, --version               prints version information and exits
  --disable-console-commands  disables console commands; by default only STOP (P) is supported
  --log-level                 the log level - trace, debug, info, warn, error [nargs=0..1] [default: "info"]
```
## Running

This example shows how to create a Jira using Rocket ported tools and integration to the MVS file system using `DD:JIRADATA`. Note that `PARM='/ /bin/sh -L'` is not required as `RKTBATCH` will run the default shell
but if the default shell is `bash` which uses `fork/exec` then data set integration will not work. Only `/bin/sh` supports local spawn.

```
//NEWJIRA  JOB  NOTIFY=&SYSUID                                 
//RKTBATCH EXEC PGM=RKTBATCH,PARMDD=PARM
//SYSOUT   DD  SYSOUT=*        
//SYSPRINT DD  SYSOUT=*        
//STDENV   DD  *               
# _BPX_SHAREAS=NO <-- uncomment to run subprocesses in separate address spaces                
/*                             
//PARM     DD  *               
/ --log-level trace /bin/sh -L                
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

`RKTBATCH` implements the MVS STOP command, making it possible to stop the utility when it is running as a started task. 
```
P MYJOB
```
