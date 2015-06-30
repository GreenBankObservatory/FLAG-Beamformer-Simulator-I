# for x in xrange(4):
#   for y in xrange(x + 1):
#       block = []
#       block.append((2*x, 2*y))
#       block.append((2*x, 2*y+1))
#       block.append((2*x+1, 2*y))
#       block.append((2*x+1, 2*y+1))
#       for z in xrange(3):
#           print block[z],
#           if (z == 1):
#               print
#   print


N = 4

n = N
m = n


ddd = []
for ni in range(n):
    dd=[]
    for mi in range(ni+1):
        d = []
        d.append((2*ni,2*mi))
        # if mi != ni:
        d.append((2*ni,2*mi+1))
        d.append((2*ni+1,2*mi))
        d.append((2*ni+1,2*mi+1))
        dd.append(d)
    ddd.append(dd)



for row in ddd:
    for block in row:
        # print len(block)
        print block[:2], " ",
    print
    for block in row:
        print block[2:], " ",
    print
    print
    # break