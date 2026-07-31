Setting up Memory Sharing
The FM may use the following sequence to set up sharing between hosts, where all
hosts are able to read and write to the shared capacity:
1. Issue Initiate Dynamic Capacity Add Request with the Selection Policy set to Free or
Contiguous or Prescriptive with the Host ID associated with the first host. The
region number must correspond to a region that is advertised as sharable.
2. If the above request is successful as indicated by a new Add Capacity Response
event in the Dynamic Capacity Event record, issue Initiate Dynamic Capacity Add
Request with Selection Policy=Enable Shared access with the Host ID associated
with the second host. The Tag field must match the Tag value used in step 1.
3. Repeat step 2 for any other hosts that need to share this memory range.
The FM may use the following example sequence to allocate a set of tagged capacity
and allow it to be initialized by a host and then shared with one or more hosts as read-
only.
1. Issue Initiate Dynamic Capacity Add Request with the Selection Policy set to Free or
Contiguous or Prescriptive with the Host ID associated with the first host. The
region number must correspond to a region that is advertised as writable and
sharable.
2. If the above request is successful, the tagged shared capacity can be initialized by
the first host.
3. Issue a Dynamic Capacity Add Reference Request for the tag associated with the
capacity. Holding this Reference prevents the tagged capacity from being freed and
sanitized in step 4.
4. After the first host has initialized the tagged shared capacity, issue an Initiate
Dynamic Capacity Release Request for the tag associated with the capacity, and
then await completion.
5. If the request in step 4 is successful as indicated by a new Release Capacity
Response event in the Dynamic Capacity Event record, the capacity associated with
the Tag is preserved but not mapped to any hosts.
6. Issue an Initiate Dynamic Capacity Add Request with Selection Policy=Enable
Shared Access with the Host ID associated with the second host, specifying a
Region that is Sharable and read-only. The Tag field must match the Tag value used
in step 1.
7. Repeat step 5 for any other hosts that need to share the tagged capacity.
8. Issue a Dynamic Capacity Remove Reference Request to remove the FM reference
to the tagged capacity.
9. To withdraw the shared capacity, issue a Initiate Dynamic Capacity Release
command for each host.
10. When the tagged capacity has been released from all hosts, if the FM does not hold
a reference, the tagged capacity will be sanitized (if appropriate) and freed, at
which point the tag no longer exists and the capacity is available for future use.