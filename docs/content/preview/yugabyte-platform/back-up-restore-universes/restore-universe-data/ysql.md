---
title: Restore universe YSQL data
headerTitle: Restore universe YSQL data
linkTitle: Restore universe data
description: Use YugabyteDB Anywhere to restore data in YSQL tables.
aliases:
  - /preview/yugabyte-platform/back-up-restore-databases/
  - /preview/yugabyte-platform/back-up-restore-universes/restore-universe-data/
menu:
  preview:
    parent: back-up-restore-universes
    identifier: restore-universe-data-1-ysql
    weight: 30
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="{{< relref "./ysql.md" >}}" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="{{< relref "./ycql.md" >}}" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

You can restore YugabyteDB universe YSQL data from a backup as follows:

1. Open your universe and select **Backups**.

2. Click **Restore Backup** to open the **Restore data to** dialog shown in the following illustration:

    <br/><br/>
    ![Restore backup - YSQL](/images/yp/restore-universe-data-ysql.png)

3. Complete the following fields:

    - **Storage**: Select the storage configuration type: `GCS Storage`, `S3 Storage`, or `NFS Storage`.
    - **Storage Location**: Specify the storage location.
    - **Universe**: Select the universe to restore.
    - **Keyspace**: Leave blank. This is used for YCQL data only.
    - **Table**: Leave blank. Backup and restore of YSQL data is for full universe data only.
    - **Parallel Threads**: Default is `8`. This value can be changed to a value between `1` and `100`.
    - **KMS Configuration**: Optional, if the backup was from a universe that has [encryption at rest enabled](../../../security/enable-encryption-at-rest), then select the KMS configuration to use.

4. Click **OK**.

    <br/>The restore begins immediately. When finished, a completed **Restore Backup** task appears in the **Tasks** tab.

5. To confirm the restore succeeded, select **Tables** to compare the original table with the table to which you restored, as per the following illustration:

  ![Tables View](/images/yp/tables-view-ysql.png)
