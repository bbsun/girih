#!/usr/bin/env python
def run_pochoir_test(dry_run, th, kernel, nx, ny, nz, nt, target_dir, outfile, pinning_cmd, pinning_args):
  import os
  import subprocess
  from string import Template
  from scripts.utils import ensure_dir    

  job_template=Template(
"""echo 'OpenMP Threads: $th' | tee $outpath
$pinning_cmd $pinning_args $exec_path $nx $ny $nz $nt | tee $outpath""")

  # set the output path
  target_dir = os.path.join(os.path.abspath("."),target_dir)
  ensure_dir(target_dir)
  outpath = os.path.join(target_dir, outfile)

  # set the executable
  if(kernel==0):
    exec_name ='3dfd'
  elif(kernel==1):
    exec_name ='3d7pt'
  elif(kernel==4):
    exec_name ='3d25pt_var'
  elif(kernel==5):
    exec_name ='3d7pt_var'
  else:
    raise
  exec_path = os.path.join(os.path.abspath("."),exec_name)
 
   
  job_cmd = job_template.substitute(th=th, nx=nx, ny=ny, nz=nz, nt=nt, kernel=kernel, outpath=outpath, 
                                    exec_path=exec_path, pinning_cmd=pinning_cmd, pinning_args=pinning_args)
 
  print job_cmd
  if(dry_run==0): sts = subprocess.call(job_cmd, shell=True)

  return job_cmd


def igs_test(dry_run, target_dir, exp_name, th, group='', params=[]): 
  from scripts.conf.conf import machine_conf, machine_info
  import itertools


  # Test using rasonable time
  # T = scale * size / perf
  # scale = T*perf/size
  desired_time = 20
  if(machine_info['hostname']=='Haswell_18core'):
    k_perf_order = {0:1000, 1:2500, 4:200, 5:1000}
  else:
    k_perf_order = {0:700, 1:1500, 4:200, 5:1000}
  k_time_scale={}
  for k, v in k_perf_order.items():
    k_time_scale[k] = desired_time*v

  points = list(range(32, 1010, 128))
  points = sorted(list(set(points)))

  kernels_limits = [1057, 1200, 0, 0, 545, 680, 289]
  radius = {0:4, 1:1, 4:4, 5:1}

  if(machine_info['hostname']=='Haswell_18core'):
    kernels_limits = [1600, 1600, 0, 0, 960, 1000, 500]

  count=0
  for kernel in [0, 1, 4, 5]:
    for N in points:
      if (N < kernels_limits[kernel]):
        key = (kernel, N)
        if key in params:
         continue
        outfile=('pochoir_kernel%d_N%d_%s_%s.txt' % (kernel, N, group, exp_name[-13:]))
        nt = max(int(k_time_scale[kernel]/(N**3/1e6)), 30)
        N = N + 2 * radius[kernel] # Pochoir takes the whole size including the halo region
        run_pochoir_test(dry_run=dry_run, th=th, kernel=kernel, nx=N, ny=N, nz=N, nt=nt, outfile=outfile, target_dir=target_dir, pinning_cmd=machine_conf['pinning_cmd'], pinning_args=machine_conf['pinning_args'])
        count = count+1
  print "experiments count =" + str(count)


def main():
  from scripts.utils import create_project_tarball, get_stencil_num
  from scripts.conf.conf import machine_conf, machine_info
  import os
  from csv import DictReader
  import time,datetime

  sockets=1 # number of processors to use in the experiments
  dry_run = 0

  time_stamp = datetime.datetime.fromtimestamp(time.time()).strftime('%Y%m%d_%H_%M')
  exp_name = "pochoir_increasing_grid_size_sockets_%d_at_%s_%s" % (sockets,machine_info['hostname'], time_stamp)  

  tarball_dir='results/'+exp_name
  create_project_tarball(tarball_dir, "project_"+exp_name)
  target_dir='results/' + exp_name 

  # parse the results to find out which of the already exist
  data = []
  data_file = os.path.join('results', 'summary.csv')
  try:
    with open(data_file, 'rb') as output_file:
      raw_data = DictReader(output_file)
      for k in raw_data:
        k['stencil'] = get_stencil_num(k)
        data.append(k)
  except:
     pass
  params = set()
  for k in data:
    try:
      params.add( (k['stencil'], int(k['Global NX'])) )
    except:
      print k
      raise

  #update the pinning information to use all cores
  th = machine_info['n_cores']*sockets

  if sockets == 1:
    pin_str = "S1:0-%d "%(th-1)
  if sockets == 2:
    pin_str = "S0:0-%d@S1:0-%d -i "%(th/2-1, th/2-1)

  for group in ['MEM', 'DATA', 'TLB_DATA', 'L2', 'L3', 'ENERGY']:
    if(machine_info['hostname']=='Haswell_18core'):
      machine_conf['pinning_args'] = " -g " + group + " -c " + pin_str + '-- numactl --physcpubind=%d-%d'%(18,18+th-1)
    elif(machine_info['hostname']=='IVB_10core'):
      machine_conf['pinning_args'] = " -g MEM -c " + pin_str + '-- numactl --physcpubind=%d-%d'%(0,th-1)

#    for k in params: print k
    igs_test(dry_run, target_dir, exp_name, th=th, params=params, group=group) 


if __name__ == "__main__":
  main()