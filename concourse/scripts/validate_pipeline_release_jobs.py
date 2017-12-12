#!/usr/bin/env python

import os
import re
import yaml

RELEASE_VALIDATOR_JOB = ['Release_Candidate']
JOBS_THAT_ARE_GATES = ['gate_compile_start', 'gate_compile_end', 'gate_icw_start',
                       'gate_icw_end', 'gate_cs_start', 'gate_cs_end', 'gate_mpp_start',
                       'gate_mpp_end', 'gate_mm_start', 'gate_mm_end', 'gate_dpm_start',
                       'gate_dpm_end', 'gate_ud_start', 'gate_ud_end', 'gate_advanced_analytics_start',
                       'gate_advanced_analytics_end', 'gate_filerep_start', 'gate_filerep_end']
JOBS_THAT_SHOULD_NOT_BLOCK_RELEASE = ['compile_gpdb_binary_swap_centos6'] + RELEASE_VALIDATOR_JOB + JOBS_THAT_ARE_GATES

pipeline_raw = open(os.environ['PIPELINE_FILE'],'r').read()
pipeline_buffer_cleaned = re.sub('{{', '', re.sub('}}', '', pipeline_raw)) # ignore concourse v2.x variable interpolation
pipeline = yaml.load(pipeline_buffer_cleaned)

jobs_raw = pipeline['jobs']
all_job_names = [job['name'] for job in jobs_raw]

release_candidate_job = [ job for job in jobs_raw if job['name'] == 'Release_Candidate' ][0]
release_qualifying_job_names = release_candidate_job['plan'][0]['passed']

jobs_that_are_not_blocking_release = [job for job in all_job_names if job not in release_qualifying_job_names]

unaccounted_for_jobs = [job for job in jobs_that_are_not_blocking_release if job not in JOBS_THAT_SHOULD_NOT_BLOCK_RELEASE]

if unaccounted_for_jobs:
    print "Please add the following jobs as a Release_Candidate dependency or ignore them"
    print "by adding them to JOBS_THAT_SHOULD_NOT_BLOCK_RELEASE in "+ __file__
    print unaccounted_for_jobs
    exit(1)
else:
    print "all jobs accounted for"
