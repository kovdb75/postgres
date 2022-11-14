
# Copyright (c) 2022, PostgreSQL Global Development Group

# Test for operation log.
#
# Some events like "bootstrap", "startup", "pg_resetwal", "promoted", "pg_upgrade".
# should be registered in operation log.
use strict;
use warnings;

use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;

my $node_primary = PostgreSQL::Test::Cluster->new('primary');
$node_primary->init(allows_streaming => 1);

# Create a primary node
$node_primary->start;

# Get server version
my $server_version = $node_primary->safe_psql("postgres", "SELECT current_setting('server_version_num');") + 0;
my $major_version = $server_version / 10000;
my $minor_version = $server_version % 100;

# Initialize standby node from backup
$node_primary->backup('primary_backup');
my $node_standby = PostgreSQL::Test::Cluster->new('standby');
$node_standby->init_from_backup($node_primary, 'primary_backup',
	has_streaming => 1);

$node_standby->start;

# Wait for standby to catch up
$node_primary->wait_for_catchup($node_standby);

# Promote the standby and stop it
$node_standby->promote;

# Stop standby
$node_standby->stop;

# Get first "pg_resetwal" event
system_or_bail('pg_resetwal', '-f', $node_standby->data_dir);

# Get second "pg_resetwal" event
system_or_bail('pg_resetwal', '-f', $node_standby->data_dir);

# Initialize a new node for the upgrade.
my $node_new = PostgreSQL::Test::Cluster->new('new');
$node_new->init;

my $bindir_new = $node_new->config_data('--bindir');
my $bindir_standby = $node_standby->config_data('--bindir');

# We want to run pg_upgrade in the build directory so that any files generated
# finish in it, like delete_old_cluster.{sh,bat}.
chdir ${PostgreSQL::Test::Utils::tmp_check};

# Run pg_upgrade.
command_ok(
	[
		'pg_upgrade', '--no-sync',         '-d', $node_standby->data_dir,
		'-D',         $node_new->data_dir, '-b', $bindir_standby,
		'-B',         $bindir_new,         '-s', $node_new->host,
		'-p',         $node_standby->port, '-P', $node_new->port
	],
	'run of pg_upgrade for new instance');

$node_new->start;

my $psql_stdout;

# Check number of event "bootstrap"
$psql_stdout = $node_new->safe_psql('postgres',	q(
SELECT
	sum(count), count(*), min(split_part(version,'.','1')), min(split_part(version,'.','2'))
FROM pg_operation_log() WHERE event='bootstrap'));
is($psql_stdout, qq(1|1|$major_version|$minor_version), 'check number of event bootstrap');

# Check number of event "startup"
$psql_stdout = $node_new->safe_psql('postgres', q(
SELECT
	count(*), min(split_part(version,'.','1')), min(split_part(version,'.','2'))
FROM pg_operation_log() WHERE event='startup'));
is($psql_stdout, qq(1|$major_version|$minor_version), 'check number of event startup');

# Check number of event "promoted"
$psql_stdout = $node_new->safe_psql('postgres', q(
SELECT
	sum(count), count(*), min(split_part(version,'.','1')), min(split_part(version,'.','2'))
FROM pg_operation_log() WHERE event='promoted'));
is($psql_stdout, qq(1|1|$major_version|$minor_version), 'check number of event promoted');

# Check number of event "pg_upgrade"
$psql_stdout = $node_new->safe_psql('postgres', q(
SELECT
	sum(count), count(*), min(split_part(version,'.','1')), min(split_part(version,'.','2'))
FROM pg_operation_log() WHERE event='pg_upgrade'));
is($psql_stdout, qq(1|1|$major_version|$minor_version), 'check number of event pg_upgrade');

# Check number of event "pg_resetwal"
$psql_stdout = $node_new->safe_psql('postgres', q(
SELECT
	sum(count), count(*), min(split_part(version,'.','1')), min(split_part(version,'.','2'))
FROM pg_operation_log() WHERE event='pg_resetwal'));
is($psql_stdout, qq(2|1|$major_version|$minor_version), 'check number of event pg_resetwal');

done_testing();
