import subprocess, wave, struct, sys, os
words = {"verse":"Verse","chorus":"Chorus","pre-chorus":"Pre-chorus","post-chorus":"Post-chorus","bridge":"Bridge","tag":"Tag","intro":"Intro","outro":"Outro",
 "instrumental":"Instrumental","interlude":"Interlude","ending":"Ending","refrain":"Refrain","vamp":"Vamp","turnaround":"Turnaround","final":"Final","last":"Last",
 "again":"Again","break":"Break","breakdown":"Breakdown","solo":"Solo","coda":"Coda","build":"Build","hook":"Hook","channel":"Channel","spontaneous":"Spontaneous","worship":"Worship",
 "down":"Down","all":"All","in":"In","out":"Out","half":"Half","time":"Time","song":"Song","next":"Next","stop":"Stop","acapella":"Acapella","hold":"Hold"}
nums = ["one","two","three","four","five","six","seven","eight","nine","ten","eleven","twelve"]
for n in nums: words[n] = n.capitalize() + "."
for k,t in words.items():
    raw = f"/tmp/v_{k}.wav"
    subprocess.run(["/tmp/pv/bin/piper","-m","/tmp/voices/en_US-lessac-medium.onnx","-f",raw,"--length-scale","0.8" if k in nums else "0.85"],input=t.encode(),check=True,capture_output=True)
    w = wave.open(raw); sr = w.getframerate(); d = w.readframes(w.getnframes()); w.close()
    s = list(struct.unpack("<%dh" % (len(d)//2), d))
    th = 400; a = next((i for i,x in enumerate(s) if abs(x)>th),0); b = len(s)-next((i for i,x in enumerate(reversed(s)) if abs(x)>th),0)
    a = max(0,a-int(sr*0.005)); b = min(len(s), b+int(sr*0.03)); s = s[a:b]
    pk = max(1,max(abs(x) for x in s)); g = 0.89*32767/pk
    s = [int(max(-32767,min(32767,x*g))) for x in s]
    f = 64
    for i in range(min(f,len(s))): s[i]=int(s[i]*i/f); s[-1-i]=int(s[-1-i]*i/f)
    o = wave.open(f"{k}.wav","wb"); o.setnchannels(1); o.setsampwidth(2); o.setframerate(sr); o.writeframes(struct.pack("<%dh"%len(s),*s)); o.close()
    print(k, sr, round(len(s)/sr,3))
