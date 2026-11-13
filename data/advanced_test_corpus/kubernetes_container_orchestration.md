# Kubernetes Container Orchestration

Kubernetes coordinates containerised workloads through a control plane and worker nodes. The kube-apiserver exposes the cluster API and validates requests. etcd stores cluster state. The kube-scheduler assigns pending Pods to suitable nodes based on resource requests, constraints, affinity, and taints. Controllers continuously compare observed state with desired state and create or repair resources.

A Pod is the smallest deployable unit and can contain one or more containers that share networking and volumes. The kubelet on a worker node ensures that assigned Pods are running. A Pod lifecycle moves through phases such as Pending, Running, Succeeded, or Failed; individual containers also have restart and readiness states.

Services provide stable virtual access to changing Pod endpoints. The cluster network generally allows Pod-to-Pod communication without requiring address translation at every hop. Network plugins implement the Container Network Interface (CNI) and provide routing, policies, and address allocation.

Kubernetes orchestration relies on declarative configuration, reconciliation loops, health probes, resource limits, rollout strategies, and persistent storage. The control plane components—kube-apiserver, etcd, and kube-scheduler—coordinate placement and desired state, while nodes execute workloads.
