#################
# Count how many top N hot items (from microbechmark access distribution) is actually
# classified as hot by LFU.

microbench_out_file = open('microbench_hotitems', 'r')
microbench_out_file_lines = microbench_out_file.readlines()

lfu_file = open('hotset_huge', 'r')
lfu_file_lines = lfu_file.readlines()

# the Q is: what portion of the "true" hot items (obtained from microbench distribution, the ground truth)
# is recognized by LFU as hot (lfu_hotset)?

# first parse the lfu_hotset, which contains virtual page numbers e.g. 7f8da0e67
lfu_hotset = set() 
for line in lfu_file_lines:
  lfu_hotset.add(int(line, 16))
  #print("addding {}".format(hex(int(line, 16))))

# then go through the microbench "true" hot items.
lfu_hits = 0
lfu_misses = 0
for line in microbench_out_file_lines:
  hotitem = line[2:-1] # skip the new line char and starting "0x"
  hotitem = int(hotitem, 16)
  hotitem_virt_page_num = hotitem >> 12
  if hotitem_virt_page_num in lfu_hotset:
    lfu_hits = lfu_hits + 1
  else:
    lfu_misses = lfu_misses + 1

print("{} hot items identified by LFU. {} missed.".format(lfu_hits, lfu_misses))

