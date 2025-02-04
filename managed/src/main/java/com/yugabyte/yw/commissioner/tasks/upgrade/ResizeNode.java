// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.ChangeInstanceType;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateNodeDetails;
import com.yugabyte.yw.forms.ResizeNodeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Collection;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.apache.commons.lang3.tuple.Pair;

@Slf4j
public class ResizeNode extends UpgradeTaskBase {

  @Inject
  protected ResizeNode(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected ResizeNodeParams taskParams() {
    return (ResizeNodeParams) super.taskParams();
  }

  @Override
  public UserTaskDetails.SubTaskGroupType getTaskSubGroupType() {
    return UserTaskDetails.SubTaskGroupType.ResizingDisk;
  }

  @Override
  public NodeDetails.NodeState getNodeState() {
    return NodeDetails.NodeState.Resizing;
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Universe universe = getUniverse();
          // Verify the request params and fail if invalid.
          taskParams().verifyParams(universe);

          Pair<List<NodeDetails>, List<NodeDetails>> nodes = fetchNodesForCluster();

          // Create task sequence to resize nodes.
          for (UniverseDefinitionTaskParams.Cluster cluster : taskParams().clusters) {

            Pair<List<NodeDetails>, List<NodeDetails>> clusterNodes =
                new ImmutablePair<>(
                    filterForCluster(nodes.getLeft(), cluster.uuid),
                    filterForCluster(nodes.getRight(), cluster.uuid));

            final UniverseDefinitionTaskParams.UserIntent userIntent = cluster.userIntent;

            String newInstanceType = userIntent.instanceType;
            UniverseDefinitionTaskParams.UserIntent currentIntent =
                universe.getUniverseDetails().getClusterByUuid(cluster.uuid).userIntent;

            final boolean instanceTypeIsChanging =
                !Objects.equals(newInstanceType, currentIntent.instanceType)
                    || taskParams().isForceResizeNode();

            if (instanceTypeIsChanging) {
              Set<NodeDetails> nodez = new HashSet<>(clusterNodes.getLeft());
              nodez.addAll(clusterNodes.getRight());
              createPreResizeNodeTasks(nodez, currentIntent.instanceType, currentIntent.deviceInfo);
            }

            createRollingNodesUpgradeTaskFlow(
                (nodez, processTypes) ->
                    createResizeNodeTasks(nodez, universe, instanceTypeIsChanging, cluster),
                clusterNodes,
                UpgradeContext.builder()
                    .reconfigureMaster(userIntent.replicationFactor > 1)
                    .runBeforeStopping(false)
                    .processInactiveMaster(false)
                    .build());

            Integer newDiskSize = null;
            if (cluster.userIntent.deviceInfo != null) {
              newDiskSize = cluster.userIntent.deviceInfo.volumeSize;
            }
            // Persist changes in the universe.
            createPersistResizeNodeTask(
                    newInstanceType, newDiskSize, Collections.singletonList(cluster.uuid))
                .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ChangeInstanceType);
          }
        });
  }

  private void createPreResizeNodeTasks(
      Collection<NodeDetails> nodes, String currentInstanceType, DeviceInfo currentDeviceInfo) {
    // Update mounted disks.
    for (NodeDetails node : nodes) {
      if (!node.disksAreMountedByUUID) {
        createUpdateMountedDisksTask(node, currentInstanceType, currentDeviceInfo)
            .setSubTaskGroupType(getTaskSubGroupType());
      }
    }
  }

  private void createResizeNodeTasks(
      List<NodeDetails> nodes,
      Universe universe,
      boolean instanceTypeIsChanging,
      UniverseDefinitionTaskParams.Cluster cluster) {

    UniverseDefinitionTaskParams.UserIntent currUserIntent =
        universe.getUniverseDetails().getClusterByUuid(cluster.uuid).userIntent;

    Integer currDiskSize = currUserIntent.deviceInfo.volumeSize;
    String currInstanceType = currUserIntent.instanceType;
    // Todo: Add preflight checks here

    // Change disk size.
    DeviceInfo deviceInfo = cluster.userIntent.deviceInfo;
    if (deviceInfo != null) {
      Integer newDiskSize = deviceInfo.volumeSize;
      // Check if the storage needs to be resized.
      if (taskParams().isForceResizeNode() || !currDiskSize.equals(newDiskSize)) {
        log.info("Resizing disk from {} to {}", currDiskSize, newDiskSize);

        // Resize the nodes' disks.
        createUpdateDiskSizeTasks(nodes)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ResizingDisk);
      } else {
        log.info(
            "Skipping resizing disk as both old and new sizes are {}, "
                + "and forceResizeNode flag is false",
            currDiskSize);
      }
    }

    // Change instance type
    String newInstanceType = cluster.userIntent.instanceType;
    if (instanceTypeIsChanging) {
      for (NodeDetails node : nodes) {
        // Check if the node needs to be resized.
        if (!taskParams().isForceResizeNode()
            && node.cloudInfo.instance_type.equals(newInstanceType)) {
          log.info("Skipping node {} as its type is already {}", node.nodeName, currInstanceType);
          continue;
        }

        // Change the instance type.
        createChangeInstanceTypeTask(node, cluster.userIntent.instanceType)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ChangeInstanceType);

        // Persist the new instance type in the node details.
        node.cloudInfo.instance_type = newInstanceType;
        createNodeDetailsUpdateTask(node)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ChangeInstanceType);
      }
    }
  }

  private SubTaskGroup createChangeInstanceTypeTask(NodeDetails node, String instanceType) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("ChangeInstanceType", executor);
    ChangeInstanceType.Params params = new ChangeInstanceType.Params();

    params.nodeName = node.nodeName;
    params.universeUUID = taskParams().universeUUID;
    params.azUuid = node.azUuid;
    params.instanceType = instanceType;

    ChangeInstanceType changeInstanceTypeTask = createTask(ChangeInstanceType.class);
    changeInstanceTypeTask.initialize(params);
    subTaskGroup.addSubTask(changeInstanceTypeTask);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  private SubTaskGroup createNodeDetailsUpdateTask(NodeDetails node) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("UpdateNodeDetails", executor);
    UpdateNodeDetails.Params updateNodeDetailsParams = new UpdateNodeDetails.Params();
    updateNodeDetailsParams.universeUUID = taskParams().universeUUID;
    updateNodeDetailsParams.azUuid = node.azUuid;
    updateNodeDetailsParams.nodeName = node.nodeName;
    updateNodeDetailsParams.details = node;
    updateNodeDetailsParams.updateCustomImageUsage = false;

    UpdateNodeDetails updateNodeTask = createTask(UpdateNodeDetails.class);
    updateNodeTask.initialize(updateNodeDetailsParams);
    updateNodeTask.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(updateNodeTask);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }
}
