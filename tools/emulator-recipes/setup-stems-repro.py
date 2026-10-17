from pathlib import Path
import sys,shutil,subprocess,json
sys.path.insert(0,'outputs/az-modding/xdj-az-emu/tools')
import azemu
src=Path('/Volumes/FLOWMATIC/STEMS/01 player dave - Confetti')
dst=Path('work/stems-repro')/src.name
dst.mkdir(parents=True,exist_ok=True)
for f in src.iterdir():
 if f.is_file() and not f.name.startswith('.'): shutil.copy2(f,dst/f.name)
p=subprocess.Popen(['tar','-C','work/stems-repro','-cf','-','.'],stdout=subprocess.PIPE,env={**__import__('os').environ,'COPYFILE_DISABLE':'1'})
r=subprocess.run(azemu._ssh_base()+['tar -xf - -C /media/usb/sda1/STEMS && sync'],stdin=p.stdout,capture_output=True)
p.stdout.close(); assert p.wait()==0 and r.returncode==0,r.stderr
rc,cfg=azemu.ssh('cat /home/root/settings/azmod.cfg'); assert rc==0 and cfg[0] in '01'
Path('work/stems-repro-original-config.txt').write_text(cfg)
rc,_=azemu.ssh('cat > /home/root/settings/azmod.cfg',data=('1'+cfg[1:]).encode()); assert rc==0
azemu.ssh('touch /home/root/settings/azmod-logs.on; rm -f /home/root/settings/azmod-community/.starting')
print('Two real stem sets copied; enabled stems and diagnostic logging for this test')
print('Restart:',azemu.restart_ep147())
