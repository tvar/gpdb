@gpinitsystem
# This behave test will heavily rely on gpdemo being available
Feature: gpinitsystem tests
    @gpinitsystem_checksum_on
    Scenario: gpinitsystem creates a cluster with data_checksums on
        Given the database is initialized with checksum "on"
        When the user runs "gpconfig -s data_checksums"
        Then gpconfig should return a return code of 0
        And gpconfig should print "Values on all segments are consistent" to stdout
        And gpconfig should print "Master  value: on" to stdout
        And gpconfig should print "Segment value: on" to stdout

    @gpinitsystem_checksum_on
    Scenario: gpinitsystem generates an output configuration file and then starts cluster with data_checksums on
        Given the cluster config is generated with data_checksums "on"
        When the user runs command "gpinitsystem -a -c ../gpAux/gpdemo/clusterConfigFile -O /tmp/output_config_file"
        And gpinitsystem should return a return code of 0
        Then verify that file "output_config_file" exists under "/tmp"
        And verify that the file "/tmp/output_config_file" contains "HEAP_CHECKSUM=on"
        And the user runs "gpinitsystem -a -I /tmp/output_config_file -l /tmp/"
        Then gpinitsystem should return a return code of 0
        When the user runs "gpconfig -s data_checksums"
        Then gpconfig should return a return code of 0
        And gpconfig should print "Values on all segments are consistent" to stdout
        And gpconfig should print "Master  value: on" to stdout
        And gpconfig should print "Segment value: on" to stdout

    @gpinitsystem_checksum_off
    Scenario: gpinitsystem creates a cluster with data_checksums off
        Given the database is initialized with checksum "off"
        When the user runs "gpconfig -s data_checksums"
        Then gpconfig should return a return code of 0
        And gpconfig should print "Values on all segments are consistent" to stdout
        And gpconfig should print "Master  value: off" to stdout
        And gpconfig should print "Segment value: off" to stdout

    @gpinitsystem_checksum_off
    Scenario: gpinitsystem generates an output configuration file and then starts cluster with data_checksums off
        Given the cluster config is generated with data_checksums "off"
        When the user runs command "gpinitsystem -a -c ../gpAux/gpdemo/clusterConfigFile -O /tmp/output_config_file"
        And gpinitsystem should return a return code of 0
        Then verify that file "output_config_file" exists under "/tmp"
        And verify that the file "/tmp/output_config_file" contains "HEAP_CHECKSUM=off"
        And the user runs "gpinitsystem -a -I /tmp/output_config_file -l /tmp/"
        Then gpinitsystem should return a return code of 0
        When the user runs "gpconfig -s data_checksums"
        Then gpconfig should return a return code of 0
        And gpconfig should print "Values on all segments are consistent" to stdout
        And gpconfig should print "Master  value: off" to stdout
        And gpconfig should print "Segment value: off" to stdout

    @gpinitsystem_standby_failure_is_only_warning
    Scenario: gpinitsystem should warn but not fail when standby cannot be instantiated
        Given the database is running
        And all the segments are running
        And the segments are synchronized
        And the standby is not initialized
        And the user runs command "rm -rf /tmp/gpinitsystemtest && mkdir /tmp/gpinitsystemtest"
        # stop db and make sure cluster config exists so that we can manually initialize standby
        And the cluster config is generated with data_checksums "1"
        When the user runs "gpinitsystem -a -c ../gpAux/gpdemo/clusterConfigFile -l /tmp/gpinitsystemtest -s localhost -P 21100 -F intentional_nonexistent_filespace:/wrong/path -h ../gpAux/gpdemo/hostfile"
        Then gpinitsystem should return a return code of 1
        And gpinitsystem should not print "To activate the Standby Master Segment in the event of Master" to stdout
        And gpinitsystem should print "Cluster setup finished, but Standby Master failed to initialize. Review contents of log files for errors." to stdout
        And sql "select * from gp_toolkit.__gp_user_namespaces" is executed in "postgres" db

    @gpinitsystem_standby_added
    Scenario: gpinitsystem should warn but not fail when standby cannot be instantiated
        Given the database is running
        And all the segments are running
        And the segments are synchronized
        And the standby is not initialized
        And the user runs command "rm -rf $MASTER_DATA_DIRECTORY/newstandby"
        And the user runs command "rm -rf /tmp/gpinitsystemtest && mkdir /tmp/gpinitsystemtest"
        And the cluster config is generated with data_checksums "1"
        When the user runs "gpinitsystem -a -c ../gpAux/gpdemo/clusterConfigFile -l /tmp/gpinitsystemtest -s localhost -P 21100 -F pg_system:$MASTER_DATA_DIRECTORY/newstandby -h ../gpAux/gpdemo/hostfile"
        Then gpinitsystem should return a return code of 0
        And gpinitsystem should print "Log file scan check passed" to stdout
        And sql "select * from gp_toolkit.__gp_user_namespaces" is executed in "postgres" db

    @gpinitsystem_hba_hostnames
    @gpinitsystem_hba_hostnames_on
    Scenario: gpinitsystem should print FQDN in pg_hba.conf when HBA_HOSTNAMES=1
        Given the cluster config is generated with HBA_HOSTNAMES "1"
        When generate cluster config file "/tmp/output_config_file"
        Then verify that the file "/tmp/output_config_file" contains "HBA_HOSTNAMES=1"
        When initialize a cluster using "/tmp/output_config_file"
        Then verify that the file "../gpAux/gpdemo/datadirs/qddir/demoDataDir-1/pg_hba.conf" contains FQDN only for trusted host
        And verify that the file "../gpAux/gpdemo/datadirs/dbfast1/demoDataDir0/pg_hba.conf" contains FQDN only for trusted host

    @gpinitsystem_hba_hostnames
    @gpinitsystem_hba_hostnames_off
    Scenario: gpinitsystem should print CIDR in pg_hba.conf when HBA_HOSTNAMES=0
        Given the cluster config is generated with HBA_HOSTNAMES "0"
        When generate cluster config file "/tmp/output_config_file"
        Then verify that the file "/tmp/output_config_file" contains "HBA_HOSTNAMES=0"
        When initialize a cluster using "/tmp/output_config_file"
        Then verify that the file "../gpAux/gpdemo/datadirs/qddir/demoDataDir-1/pg_hba.conf" contains CIDR only for trusted host
        And verify that the file "../gpAux/gpdemo/datadirs/dbfast1/demoDataDir0/pg_hba.conf" contains CIDR only for trusted host

    @gpinitsystem_hba_hostnames
    @gpinitsystem_hba_hostnames_on_with_standby
    Scenario: gpinitsystem should print FQDN in pg_hba.conf for standby when HBA_HOSTNAMES=1
        Given the database is running
        And all the segments are running
        And the segments are synchronized
        And the standby is not initialized
        And ensure the standby directory does not exist
        And the cluster config is generated with HBA_HOSTNAMES "1"
        When generate cluster config file "/tmp/output_config_file"
        Then verify that the file "/tmp/output_config_file" contains "HBA_HOSTNAMES=1"
        When initialize a cluster with standby using "/tmp/output_config_file"
        Then verify that the file "../gpAux/gpdemo/datadirs/qddir/demoDataDir-1/pg_hba.conf" contains FQDN only for trusted host
        And verify that the file "../gpAux/gpdemo/datadirs/dbfast1/demoDataDir0/pg_hba.conf" contains FQDN only for trusted host
        And verify that the file "../gpAux/gpdemo/datadirs/qddir/demoDataDir-1/newstandby/pg_hba.conf" contains FQDN only for trusted host
