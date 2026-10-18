cd /mnt/user-data/outputs && rm -f rev6exe.zip && python3 -c "
import zipfile;z=zipfile.ZipFile('rev6exe.zip','w',zipfile.ZIP_DEFLATED)
z.write('/home/claude/ht/build-win/hydrothunder.exe','hydrothunder.exe')
z.write('/home/claude/ht/sdk/out/win-amd64/librexruntime.dll','librexruntime.dll')
z.write('/home/claude/ht/sdk/out/win-amd64/rexgpu-xenos.dll','rexgpu-xenos.dll');z.close()" && ls -la rev6exe.zip
