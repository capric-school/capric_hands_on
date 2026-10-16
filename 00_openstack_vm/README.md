
## The Dashboard
You can use the provided credentials to log in to [Xerces](https://xerces.ericsson.net/) [here]().

Once you log in to [Xerces](https://xerces.ericsson.net/) you will be greeted with a project overview that shows you how much of your allocation you are using in the currently selected project.

Items to note in the web ui:

- top right shows your username, and this is where you can download an rc-file (used by the command-line interface) and f.ex. sign out.
- top left shows the current project. If you are a member of several projects, you can switch between them here.
- left side shows all the tools needed to deploy networks, VMs, and basically everything you need.

As a first-time user, there are a few preparatory steps that you should go through, we've put them here in what we believe is a good order.

### Adding SSH-keys
You should add an SSH key; it will be used to log in to your VM once it's up and running.

On the left side, under Compute > Key Pairs, you can either create or import a key.

If you choose to create a key, it will be downloaded and you'll have to select it as you connect to the VM, one example could be:

```
ssh -i ~/Downloads/my-key -l ubuntu <vm ip>
```

## Adding SSH to Security Groups

Go to Network > Security Groups.

You now have a choice of modifying "default" or creating a new security group. Most of it is the same; however, you can skip the creation step if you want to and just click on "default".

### Creating a security group

Click on "Create Security Group" and give it a name, in our case "ssh".

The egress (output) rules that it adds are already part of "default" so you can remove them if you want to. Click on "Add Rule", select the "Rule" by clicking on "Custom TCP Rule" and select SSH at the bottom of the list.

You can now give it a description and limit it to a specific subnet/machine if you want to, and then click "Add"

## Setting up network

We highly recommend creating your own network, so that all your VM:s can communicate with each other on a "local" network and anything internet-facing will have a "floating-ip" instead (basically a 1:1 NAT setup).

There are also security groups, these can be used to limit the interactions different machines can have with each other.

One example would be the common web frontend and database backend: the web servers need to be reachable from the internet, but the database servers shouldn't be, and communication between the web server and database server should be limited to only the database port, while the database servers might have more ports enabled to do replication, etc.

We really do recommend that you take a look at Security Groups.

### Create a Network

Go to Network > Networks and click on "Create Network".

You can name it anything and select any IP series, but we recommend that you select one of the reserved local IP ranges:

- 10.0.0.0/8
- 100.64.0.0/10
- 172.16.0.0/12
- 192.168.0.0/16

You can only collide in IP series in your own Project so don't worry too much about it.

### Network
Name your network

### Subnet
Name your subnet and specify which IP series you want to use, gateway will be set automatically if you don't specify one.

#### Subnet Details
Configure the DNS servers you want to use, to use Google DNS:

```
8.8.8.8
8.8.4.4
```

or, to use Cloudflare:

```
1.1.1.1
```

or basically use your preference in public DNS server.

### Create a Router
We need to have a router, just like you do at home; this will be used to connect your network to the internet or to connect several different internal networks.

Go to Network > Routers, and click on "Create Router"

#### Create Router
Give it a name and select the "External Network", in this case "internet", then click on "Create Router".

Click on the router we just created, and then on "Interfaces"

#### Interfaces
Click on Add Interface

#### Add Interface
Click on "Select Subnet" and select the Subnet you created before, and then "Submit".

Congratulations, you have now created a router between your internal network and the internet - it will NAT your connections but you can also assign Floating IPs, more about them later.

## Creating a VM
Go to Compute > Instances and click on "Launch Instance".

### Launch Instance
We highly recommend that you use volumes, as you can see in "Source" there is a setting called "Create New Volume" that should be set to yes. This enables you to delete the VM and create a new VM using the old Volume, like moving a harddisk between to computers. That's also why "Delete Volume on Instance Delete" should be "No".

We'll go through the tabs you need to look at here:

- Details
  - Name the instance
  - You can create multiple instances by setting the count here, OpenStack will add a -1 and -2, etc after the name.

- Source
  - Select Boot Source
    - Usually image, unless you've had a VM running before and you want to attach an old Volume to it.
    - Set the size of the Volume
    - Select a source image for this VM

- Flavor
  - Flavor is the size of the VM, as in memory and CPUs. We have a naming convention where c = cores and m = memory in GB; select one that seems to fit

- Network
  - Select the network you created earlier

- Security Groups
  - If you have any specific security groups you want to assign to the machine, such as "ssh" that you might have created earlier

- Key Pair
  - Ensure that the correct key is selected; if you have multiple, it will not select one by default.

Now you can launch the instance.

### Associate Floating IP
Adding a floating IP creates a 1:1 NAT to that VM. It's basically connected to the internet.

In Compute > Instances find your instance. On the right-hand side of "Create Snapshot" there is an arrow that points down; click on it. And click on "Associate Floating IP"; now you can either click "Select an IP address" and find one that you have used previously or the "+" sign to allocate one.

Note: Floating IPs will be culled occasionally, so there is no guarantee that any unused Floating IP will remain.

## Connect to your new VM

You can now connect to your VM using SSH, things to know:

- ``-i <identity>``
  - specify a key file
- ``-l <login>``
  - or user@host, same thing

The username differs between distros, ie ubuntu for Ubuntu and fedora for Fedora :)

Example command line:

```
ssh -i ~/Downloads/my_key -l ubuntu <ip>
```
