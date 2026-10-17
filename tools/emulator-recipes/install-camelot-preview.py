from pathlib import Path
import sys
sys.path.insert(0,'outputs/az-modding/xdj-az-emu/tools')
import azemu
base=Path('outputs/az-modding/camelot-jog-preview/artwork')
for p in base.glob('*.jpg'):
    azemu.push_file(str(p),'/media/usb/sda1/PIONEER/Artwork/camelot-preview/'+p.name)
sql=[]
for key,track in [('7A','/Contents/Hypho/Dingle Bat/01 Access Denied.mp3'),('2A','/Contents/player dave/and then i started floating/01 player dave - Confetti.flac')]:
    art='/PIONEER/Artwork/camelot-preview/'+key+'.jpg'
    sql.append("INSERT INTO image (image_id,path) SELECT (SELECT COALESCE(MAX(image_id),0)+1 FROM image),'"+art+"' WHERE NOT EXISTS (SELECT 1 FROM image WHERE path='"+art+"');")
    sql.append("UPDATE content SET image_id=(SELECT image_id FROM image WHERE path='"+art+"' LIMIT 1) WHERE path='"+track.replace("'","''")+"';")
p=Path('work/camelot-preview.sql'); p.write_text('\n'.join(sql)+'\n')
azemu.push_file(str(p),'/media/usb/sda1/PIONEER/azspool/camelot-preview.sql')
print('Copied preview artwork and prepared SQL to emulator USB only')
