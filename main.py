import requests
import json
from google import genai
import os
import subprocess,re
import yaml

os.chdir("..")
from dotenv import load_dotenv ,find_dotenv
load_dotenv(find_dotenv())

iterations=5
errors=[]
warnings=[]

api_key=os.environ("google_ai_api_key")

with open("config.json",'r') as f:
    data=json.load(f)
    
questions=data['questions']
style=data['coding-style']
client=genai.Client(api_key=api_key)

for i in range(iterations):
    for j in range(len(questions)):
        if i==0:
            response=client.models.generate_content(
                model='gemini-2.5-flash',contents=f"{questions[0]} - only provide code and refer to {style} for proper coding style and nothing else also keep in mind to remove c ``` at starting and ``` in the end of the code and keep author name as Bhanu"
            )

            with open(f"temp_ldd/ldd_{j}.c",'w') as f:
                f.write(response.text)
            with open("ldd.c",'w') as f:
                f.write(response.text)
        else:
            with open(f"fixes/tidy_fixes_{j}.yaml",'r') as f:
                fixes=yaml.safe_load(f)
                
            with open(f"temp_ldd/ldd_{j}.c",'r') as f:
                fix_code=f.read()
                
            response=client.models.generate_content(
                model='gemini-2.5-flash',contents=f"Given <br> {fix_code} <br> ,these are the errors in it:{fixes}, fix the code and only provide code and nothing else also keep in mind to remove c ``` at starting and ``` in the end of the code and keep author name as Bhanu"
            )

            with open(f"temp_ldd/ldd_{j}.c",'w') as f:
                f.write(response.text)
            
            with open(f"ldd.c","w") as f:
                f.write(response.text)
                
        
        cmd = ["clang-tidy","ldd.c","-p",".","--extra-arg=-I/lib/modules/$(uname -r)/build/include",f"-export-fixes=fixes/tidy_fixes_{j}.yaml"]
        try:
            out =subprocess.run(cmd)
            text = out.stdout
            
            warning = len(re.findall(r":\d+:\d+:\s+warning:", text))
            error   = len(re.findall(r":\d+:\d+:\s+error:", text))
            if i==0:
                warnings.append(warning)
                errors.append(error)            
            else:
                warnings[j]=warning
                errors[j]=error
                
        except Exception as e:
            print(e)
        
        
        