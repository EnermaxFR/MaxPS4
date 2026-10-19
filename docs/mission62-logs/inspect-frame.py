from PIL import Image
import sys
im=Image.open(sys.argv[1]).convert('RGB').resize((480,270),Image.Resampling.NEAREST)
for name,predicate in [('Kero',lambda r,g,b:g>180 and r<70 and b<90),('red',lambda r,g,b:r>200 and g<90 and b<100)]:
 points={(x,y) for y in range(im.height) for x in range(im.width) if predicate(*im.getpixel((x,y)))}
 groups=[]
 while points:
  seed=points.pop();stack=[seed];group=[seed]
  while stack:
   x,y=stack.pop()
   for neighbor in [(x-1,y),(x+1,y),(x,y-1),(x,y+1)]:
    if neighbor in points:points.remove(neighbor);stack.append(neighbor);group.append(neighbor)
  if len(group)>2:groups.append((len(group),min(x for x,y in group),min(y for x,y in group),max(x for x,y in group),max(y for x,y in group)))
 print(name,sorted(groups,reverse=True))
