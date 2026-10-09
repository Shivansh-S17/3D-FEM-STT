import json
import numpy as np
from sys import argv, exit
from scipy.sparse import csc_array, lil_matrix
from scipy.sparse.linalg import spsolve
#from matplotlib.pyplot import spy, show
from abc import ABC, abstractmethod
import pyvista as pv
from C3D8local import *

class mesh_c3d8(ABC):
    def __init__(self, xnodes: np.array, ynodes: np.array, znodes: np.array, num_dofs_per_node: int = 3):
        self.xnodes = xnodes
        self.ynodes = ynodes
        self.znodes = znodes
        self.num_dofs_per_node = num_dofs_per_node

    def getNumNodes(self) -> int:
        return self.xnodes.size * self.ynodes.size * self.znodes.size

    def getNumNodes_x(self) -> int:
        return self.xnodes.size
    
    def getNumNodes_y(self) -> int:
        return self.ynodes.size

    def getNumNodes_z(self) -> int:
        return self.znodes.size
    
    def getNumElements_x(self) -> int:
        return (self.xnodes.size - 1)
    
    def getNumElements_y(self) -> int:
        return (self.ynodes.size - 1)
    
    def getNumElements_z(self) -> int:
        return (self.znodes.size - 1)
    
    def getNumElements(self) -> int:
        return (self.xnodes.size - 1) * (self.ynodes.size - 1) * (self.znodes.size - 1)

    def getNumNodesPerElement(self) -> int:
        return 8

    def getNumDofs(self) -> int: 
        return self.getNumNodes() * self.num_dofs_per_node
    
    def getNumDofsPerElement(self) -> int: 
        return self.getNumNodesPerElement()*self.num_dofs_per_node
    
    @abstractmethod
    def getCoordinatesOfNode(self, nodeNum): 
        pass
    
    @abstractmethod
    def getNodesOfElement(self, elementNum): 
        pass
    
    @abstractmethod
    def getDofsOfNode(self, nodeNum):
        pass
    
    @abstractmethod
    def getCoordinatesOfNodesOfElement(self, elementNum: int):
        pass
    
    @abstractmethod
    def getDofsOfElement(self, elementNum: int):
        pass
    
    @abstractmethod
    def getBottomElements(self):
        pass
    
    #@abstractmethod
    def getElementBounds(self, elemNum):
        pass
    
    #@abstractmethod
    def isTopElement(self, elemNum):
        pass
    
    #@abstractmethod
    def isBottomElement(self, elemNum):
        pass
    
class mesh_c3d8_enum_xzy(mesh_c3d8): #First enumerate XZ plane for a fixed Y, then move to next Y. So, XZ is the "side plane"
    def __init__(self, xnodes, ynodes, znodes, num_dofs_per_node=3):
        super().__init__(xnodes, ynodes, znodes, num_dofs_per_node)
    
    def getCoordinatesOfNode(self, nodeNum: int) -> np.array: 
        assert nodeNum <= self.getNumNodes() - 1, f"Node number {nodeNum} exceeds the maximum number of nodes, which is {self.getNumNodes()-1}"
        
        numNodesOnSidePlane = self.getNumNodes_x() * self.getNumNodes_z()
        numNodesBeforeThisNodeOnThisPlane = nodeNum % numNodesOnSidePlane
        numPlanesBeforeThisPlane = nodeNum // numNodesOnSidePlane 
        numNodesInZDirBeforeThisNodeOnThisPlane = numNodesBeforeThisNodeOnThisPlane // self.getNumNodes_x()
        numNodesInXDirBeforeThisNodeOnThisPlane = numNodesBeforeThisNodeOnThisPlane % self.getNumNodes_x()
        
        xCoordinateOfThisNode = self.xnodes[numNodesInXDirBeforeThisNodeOnThisPlane]
        zCoordinateOfThisNode = self.znodes[numNodesInZDirBeforeThisNodeOnThisPlane]
        yCoordinateOfThisNode = self.ynodes[numPlanesBeforeThisPlane]
        
        return np.array([xCoordinateOfThisNode, yCoordinateOfThisNode, zCoordinateOfThisNode])

    def getNodesOfElement(self, elementNum: int) -> np.array: 
        assert elementNum <= self.getNumElements() - 1, f"Element number {elementNum} exceeds the maximum number of elements, which is {self.getNumElements()-1}"
        
        numNodesOnSidePlane = self.getNumNodes_x() * self.getNumNodes_z()
        numElementsOnSidePlane = self.getNumElements_x() * self.getNumElements_z()
        numElementsBeforeThisElementOnThisPlane = elementNum % numElementsOnSidePlane
        numPlanesBeforeThisPlane = elementNum // numElementsOnSidePlane
        numElementsInZDirBeforeThisElementOnThisPlane = numElementsBeforeThisElementOnThisPlane // self.getNumElements_x()
        numElementsInXDirBeforeThisElementOnThisPlane = numElementsBeforeThisElementOnThisPlane % self.getNumElements_x()
        
        numNodesBeforeThisElement = numNodesOnSidePlane * numPlanesBeforeThisPlane + numElementsInZDirBeforeThisElementOnThisPlane * self.getNumNodes_x() + numElementsInXDirBeforeThisElementOnThisPlane #This is the node number of the first node due to zero indexing
        
        topSurfaceBottomLeftNodeInLocalOrder = numNodesBeforeThisElement
        topSurfaceTopLeftNodeInLocalOrder = topSurfaceBottomLeftNodeInLocalOrder + 1
        topSurfaceTopRightNodeInLocalOrder = topSurfaceTopLeftNodeInLocalOrder + numNodesOnSidePlane #Shift by one plane
        topSurfaceBottomRightNodeInLocalOrder = topSurfaceTopRightNodeInLocalOrder - 1
        bottomSurfaceBottomLeftNodeInLocalOrder = topSurfaceBottomLeftNodeInLocalOrder + self.getNumNodes_x()
        bottomSurfaceTopLeftNodeInLocalOrder = bottomSurfaceBottomLeftNodeInLocalOrder + 1
        bottomSurfaceTopRightNodeInLocalOrder = bottomSurfaceTopLeftNodeInLocalOrder + numNodesOnSidePlane #Shift by one plane
        bottomSurfaceBottomRightNodeInLocalOrder = bottomSurfaceTopRightNodeInLocalOrder - 1
        
        getNodesOfElementInLocalOrder = np.array([topSurfaceBottomLeftNodeInLocalOrder, topSurfaceTopLeftNodeInLocalOrder, topSurfaceTopRightNodeInLocalOrder, topSurfaceBottomRightNodeInLocalOrder, bottomSurfaceBottomLeftNodeInLocalOrder, bottomSurfaceTopLeftNodeInLocalOrder, bottomSurfaceTopRightNodeInLocalOrder, bottomSurfaceBottomRightNodeInLocalOrder])
        
        return getNodesOfElementInLocalOrder
    
    def getDofsOfNode(self, nodeNum: int) -> np.array:
        firstDOf = nodeNum * self.num_dofs_per_node
        Dofs = np.arange(firstDOf, firstDOf + self.num_dofs_per_node, dtype=int)
        return Dofs
    
    def getCoordinatesOfNodesOfElement(self, elementNum: int) -> np.array:
        nodesOfElement = self.getNodesOfElement(elementNum)
        coordinatesOfNodesOfThisElementInLocalOrder = np.zeros((self.getNumNodesPerElement(), 3), dtype=float)
        for i, node in enumerate(nodesOfElement):
            coordinatesOfNodesOfThisElementInLocalOrder[i,:] = self.getCoordinatesOfNode(node)
        
        return coordinatesOfNodesOfThisElementInLocalOrder
    
    def getDofsOfElement(self, elementNum: int) -> np.array:
        nodesOfElement = self.getNodesOfElement(elementNum)
        dofsOfThisElementInLocalOrder = np.zeros((self.getNumDofsPerElement(),), dtype=int)
        for i, node in enumerate(nodesOfElement):
            dofsOfThisElementInLocalOrder[self.num_dofs_per_node*i:self.num_dofs_per_node*(i+1)] = self.getDofsOfNode(node)
        
        return dofsOfThisElementInLocalOrder
    
    def getBottomElements(self) -> np.array:
        bottomElements = np.zeros((self.getNumElements_x() * self.getNumElements_y(),), dtype=int)
        numElementsInSidePlane = self.getNumElements_x() * self.getNumElements_z()
        firstRowOfElements = np.arange(self.getNumElements_x() * (self.getNumElements_z() - 1), self.getNumElements_x() * self.getNumElements_z(), dtype=int)
        bottomElements[0:self.getNumElements_x()] = firstRowOfElements
        
        for i in range(1,self.getNumElements_y()):
            bottomElements[i*self.getNumElements_x():(i+1)*self.getNumElements_x()] = firstRowOfElements + i*numElementsInSidePlane

        return bottomElements
    
    def getAllNodeCoordinates(self) -> np.array: #Returns in global order of nodes
        num_nodes = self.getNumNodes()
        coords = np.zeros((num_nodes,3))
        for i in range(num_nodes):
            coords[i,:] = self.getCoordinatesOfNode(i)
        
        return coords

    def getElementBounds(self, elementNum: int) -> np.array: #For load computations
        #Helper function for getRectangleBoundsInElement
        #Return bounds as [xmin, xmax, ymin, ymax, zmin, zmax]
        coordinatesOfNodesOfThisElementInLocalOrder = self.getCoordinatesOfNodesOfElement(elementNum)
        return np.array([coordinatesOfNodesOfThisElementInLocalOrder[0,0], coordinatesOfNodesOfThisElementInLocalOrder[1,0], coordinatesOfNodesOfThisElementInLocalOrder[0,1], coordinatesOfNodesOfThisElementInLocalOrder[3,1], coordinatesOfNodesOfThisElementInLocalOrder[0,2], coordinatesOfNodesOfThisElementInLocalOrder[4,2]])
        
    def getSizeOfElement(self, elementNum: int) -> np.array:
        #Return size as [a, b, c]
        coordinatesOfNodesOfThisElementInLocalOrder = self.getCoordinatesOfNodesOfElement(elementNum)
        a = coordinatesOfNodesOfThisElementInLocalOrder[1,0] - coordinatesOfNodesOfThisElementInLocalOrder[0,0]
        b = coordinatesOfNodesOfThisElementInLocalOrder[3,1] - coordinatesOfNodesOfThisElementInLocalOrder[0,1]
        c = coordinatesOfNodesOfThisElementInLocalOrder[4,2] - coordinatesOfNodesOfThisElementInLocalOrder[0,2]

        return np.array([a,b,c])

    def partitionRectangleToElements(self, rectangleBounds: np.array) -> np.array: #For load computations
        #Return bounds as [xmin, xmax, ymin, ymax], assumes z = 0
        numElementsInSidePlane = self.getNumElements_x() * self.getNumElements_z()
        x_lower_index = np.sum(self.xnodes <= rectangleBounds[0]) - 1
        x_upper_index = np.sum(self.xnodes < rectangleBounds[1])
        y_lower_index = np.sum(self.ynodes <= rectangleBounds[2]) - 1
        y_upper_index = np.sum(self.ynodes < rectangleBounds[3])

        intersecting_elements = (np.arange(x_lower_index, x_upper_index) + np.arange(y_lower_index, y_upper_index)[:, np.newaxis]*numElementsInSidePlane).flatten() #Thanks, Gemini!

        return intersecting_elements
    
    def intervalOverlap(self, x1: np.array, x2: np.array) -> np.array:
        #Helper function for getRectangleBoundsInElement
        overlap_min = max(x1[0], x2[0])
        overlap_max = min(x1[1], x2[1])

        if overlap_min <= overlap_max:
            return np.array([overlap_min, overlap_max])
        else:
            return np.array([])
    
    def getRectangleBoundsInElement(self, elementNum: int, rectangleBounds: np.array) -> np.array: #For load computations
        #Return overlap bounds as [xmin, xmax, ymin, ymax], assumes z = 0
        elementBounds = self.getElementBounds(elementNum) #Bounds as [xmin, xmax, ymin, ymax, zmin, zmax]
        x_overlap = self.intervalOverlap(elementBounds[0:2], rectangleBounds[0:2])
        y_overlap = self.intervalOverlap(elementBounds[2:4], rectangleBounds[2:4])

        assert (x_overlap.size != 0) and (x_overlap.size != 0), f"Error in finding the bounds of the load within element number {elementNum}!"

        return np.concatenate((x_overlap, y_overlap))
    
    def getLocalRectangleBoundsInElement(self, elementNum: int, rectangleBounds: np.array) -> np.array: #For load computations
        #Returns overlap bounds as [xmin, xmax, ymin, ymax] but with respect to the local coordinates of the element i.e., bottom left is (0,0,0), assumes z=0
        elementBounds = self.getElementBounds(elementNum) #Bounds as [xmin, xmax, ymin, ymax, zmin, zmax]
        overlapGlobal = self.getRectangleBoundsInElement(elementNum, rectangleBounds)
        overlapLocal = overlapGlobal
        overlapLocal[0:2] = overlapLocal[0:2] - elementBounds[0]
        overlapLocal[2:4] = overlapLocal[2:4] - elementBounds[2]
        return overlapLocal

class RectangleLoad:
    def __init__(self,q,rectangleBounds):
        #Load is only at the top surface
        self.q = q
        self.rectangleBounds = rectangleBounds #In order of [xmin, xmax, ymin, ymax] of rectangle

        assert rectangleBounds[1]>rectangleBounds[0], f"Error in load definition: x2 = {self.rectangleBounds[1]} < x1 = {self.rectangleBounds[0]}. The load must always be defined as x2 > x1."
        assert rectangleBounds[3]>rectangleBounds[2], f"Error in load definition: y2 = {self.rectangleBounds[3]} < y1 = {self.rectangleBounds[2]}. The load must always be defined as y2 > y1."
        assert q>0.0, f"Error in load definition: pressure q = {self.q} is not positive."

    def getLoadArea(self) -> float:
        return (self.rectangleBounds[1] - self.rectangleBounds[0])*(self.rectangleBounds[3] - self.rectangleBounds[2])
    
    def getLoadMagnitude(self) -> float:
        return self.getLoadArea()*self.q
    
    def test_load(self):
        print(f"The load has a pressure of {self.q} in bounds of {self.rectangleBounds}. The area of this load if {self.getLoadArea()}, and so the total load is {self.getLoadMagnitude()}.")

class SlabProperties:
    def __init__(self, Emod_, nu_):
        self.Emod = Emod_
        self.nu = nu_
    
    def test(self):
        print(f"Elastic modulus = {self.Emod}, Poisson ratio = {self.nu}")

class SubgradeProperties:
    def __init__(self, Kx, Ky, Kz):
        self.Kx = Kx
        self.Ky = Ky
        self.Kz = Kz
    
    def test(self):
        print(f"Kx = {self.Kx}, Ky = {self.Ky}, Kz = {self.Kz}")

class CustomOptions:
    def __init__(self, reduced_integration):
        self.reduced_integration = reduced_integration

def read_mesh_inputs(filename: str, ndofs_per_node: int) -> mesh_c3d8_enum_xzy:
    try:
        with open(filename, 'r') as jsonFS:
            jsonData = json.load(jsonFS)
            xnodes = np.array(jsonData["nodes"]["x"], dtype=float)
            ynodes = np.array(jsonData["nodes"]["y"], dtype=float)
            znodes = np.array(jsonData["nodes"]["z"], dtype=float)
            return mesh_c3d8_enum_xzy(xnodes, ynodes, znodes, ndofs_per_node)
    except Exception as e:
        print(f"An unexpected error occurred: {e}"); exit()

def read_slab_properties(filename: str) -> SlabProperties:
    try:
        with open(filename, 'r') as jsonFS:
            jsonData = json.load(jsonFS)
            Emod = jsonData["material"]["Emod"]
            nu = jsonData["material"]["nu"]
            return SlabProperties(Emod, nu)
    except Exception as e:
        print(f"An unexpected error occurred: {e}"); exit()

def read_subgrade_properties(filename: str) -> SubgradeProperties:
    try:
        with open(filename, 'r') as jsonFS:
            jsonData = json.load(jsonFS)
            Kx = jsonData["subgrade"]["Kx"]
            Ky = jsonData["subgrade"]["Ky"]
            Kz = jsonData["subgrade"]["Kz"]
            return SubgradeProperties(Kx,Ky,Kz)
    except Exception as e:
        print(f"An unexpected error occurred: {e}"); exit()

def read_custom_options(filename: str) -> CustomOptions:
    try:
        with open(filename, 'r') as jsonFS:
            jsonData = json.load(jsonFS)
            reduced_integration = jsonData["options"]["reduced_integration"]
            return CustomOptions(reduced_integration)
    except Exception as e:
        print(f"An unexpected error occurred: {e}"); exit()

def read_loads(filename: str) -> list:
    try:
        with open(filename, 'r') as jsonFS:
            jsonData = json.load(jsonFS)
            loads = []
            for load in jsonData["loads"]:
                bounds = np.array([load["x1"], load["x2"], load["y1"], load["y2"]])
                loads.append(RectangleLoad(load["q"], bounds))

        return loads

    except Exception as e:
        print(f"Error reading load file: {e}"); exit()

def post_processor(mesh: mesh_c3d8_enum_xzy, slab_properties: SlabProperties, Ug: np.array):
    nodal_stress = np.zeros((mesh.getNumNodes(),6)) #sxx, syy, szz, sxy, syz, szx - see WxMaxima code for other in B matrix
    nodal_strain = np.zeros((mesh.getNumNodes(),6)) #exx, eyy, ezz, exy, eyz, ezx - see WxMaxima code for other in B matrix
    nodal_counts = np.zeros((mesh.getNumNodes(),)) #Needed for averaging later

    Dmat_val = Dmat(slab_properties.Emod, slab_properties.nu)

    #Loop over each element
    for ielem in range(mesh.getNumElements()):
        #Get info about the element (needed for B matrices)
        dofsOfElement = mesh.getDofsOfElement(ielem)
        elementSize = mesh.getSizeOfElement(ielem)
        nodesOfElement = mesh.getNodesOfElement(ielem)
        coordsOfNodesOfElement = mesh.getCoordinatesOfNodesOfElement(ielem)
        Ue = Ug[dofsOfElement]

        #Calculate stresses and strains at each node of this element, and also count the number of times the node is visited for averaging later
        for i_loc, I_node in enumerate(nodesOfElement):
            localCoordinatesOfNode = coordsOfNodesOfElement[i_loc,:] - coordsOfNodesOfElement[0,:] #Bottom left top surface node is at (0,0,0) locally
            Bmat = Blocal(localCoordinatesOfNode[0], localCoordinatesOfNode[1], localCoordinatesOfNode[2], elementSize[0], elementSize[1], elementSize[2])
            strains = Bmat @ Ue
            stresses = Dmat_val @ strains

            nodal_strain[I_node] += strains
            nodal_stress[I_node] += stresses
            nodal_counts[I_node] += 1
        
    #Average the nodal stresses and strains 
    assert np.all(nodal_counts != 0), "It appears as though at least one node was never visited during post-processing. Something is wrong."
    nodal_strain /= nodal_counts[:, np.newaxis]
    nodal_stress /= nodal_counts[:, np.newaxis]

    return nodal_strain, nodal_stress

def export_to_vtk(mesh: mesh_c3d8_enum_xzy, slab_properties: SlabProperties, Ug: np.array, filename="output.vtk"):
    # We pass (X, Y, Z) - remember your y is horizontal (X-screen), x is vertical (Y-screen)
    grid = pv.RectilinearGrid(mesh.ynodes, mesh.xnodes, mesh.znodes)
    nodal_strain, nodal_stress = post_processor(mesh, slab_properties, Ug)
    Ug = Ug.reshape((mesh.getNumNodes(), mesh.num_dofs_per_node))

    grid.point_data["u"] = Ug[:,0]
    grid.point_data["v"] = Ug[:,1]
    grid.point_data["w"] = Ug[:,2]  
    grid.point_data["sxx"] = nodal_stress[:,0] 
    grid.point_data["syy"] = nodal_stress[:,1] 
    grid.point_data["szz"] = nodal_stress[:,2] 
    grid.point_data["sxy"] = nodal_stress[:,3] 
    grid.point_data["syz"] = nodal_stress[:,4] 
    grid.point_data["szx"] = nodal_stress[:,5] 
    grid.point_data["exx"] = nodal_strain[:,0] 
    grid.point_data["eyy"] = nodal_strain[:,1] 
    grid.point_data["ezz"] = nodal_strain[:,2] 
    grid.point_data["exy"] = nodal_strain[:,3] 
    grid.point_data["eyz"] = nodal_strain[:,4] 
    grid.point_data["ezx"] = nodal_strain[:,5] 

    grid.save(filename)
    print(f"File saved: {filename}")

def export_to_csv(mesh: mesh_c3d8_enum_xzy, slab_properties: SlabProperties, Ug: np.array, filename="output.csv"):
    import pandas as pd
    
    nodal_strain, nodal_stress = post_processor(mesh, slab_properties, Ug)
    Ug = Ug.reshape((mesh.getNumNodes(), mesh.num_dofs_per_node))
    coords = mesh.getAllNodeCoordinates()
    data = {
        'x' : coords[:,0],
        'y' : coords[:,1],
        'z' : coords[:,2],
        'u': Ug[:,0],
        'v': Ug[:,1],
        'w': Ug[:,2],
        'sxx': nodal_stress[:,0],
        'syy': nodal_stress[:,1],
        'szz': nodal_stress[:,2],
    }
    
    df = pd.DataFrame(data)
    df.to_csv(filename, index=False)
    print(f'Data printed to {filename}')
    

if __name__ == "__main__":
    if len(argv) < 2:
        print("Usage: python your_script_name.py <input_file.json>"); exit()
    filename = argv[1]
    dofs_per_node = 3
    
    print('**** Reading mesh info')
    mesh = read_mesh_inputs(filename, dofs_per_node)

    print('**** Reading slab properties')
    slab_properties = read_slab_properties(filename)
    print('Success!')

    print('**** Reading subgrade properties')
    subgrade_properties = read_subgrade_properties(filename)
    print('Success!')

    print('**** Reading loads')
    loads = read_loads(filename)
    print('Success!')

    print('**** Reading options')
    custom_options = read_custom_options(filename)
    print('Success!')

    print('**** Assembling Global Stiffness Matrix (Kg)')
    #Build as LIL then cast to CSR for solving, similar implemetation in Eigen
    Kg = lil_matrix((mesh.getNumDofs(), mesh.getNumDofs()), dtype=float)
    for ielem in range(mesh.getNumElements()):
        dofs = mesh.getDofsOfElement(ielem)
        elementSize = mesh.getSizeOfElement(ielem)

        if custom_options.reduced_integration:
            Ke = KReducedlocal(slab_properties.Emod, slab_properties.nu, elementSize[0], elementSize[1], elementSize[2])
        else:
            Ke = Klocal(slab_properties.Emod, slab_properties.nu, elementSize[0], elementSize[1], elementSize[2])
        
        for i_loc, I in enumerate(dofs):
            for j_loc, J in enumerate(dofs):
                if True: #I >= J: #Since Kg is symmetric, store only the lower triangle. Python has no good way to solve this, so I am storing the whole thing here. Can be done better in Eigen!
                    Kg[I, J] += Ke[i_loc, j_loc]
    
    for ielem in mesh.getBottomElements():
        #Add subgrade stiffness only for bottom elements
        dofs = mesh.getDofsOfElement(ielem)
        elementSize = mesh.getSizeOfElement(ielem)

        if custom_options.reduced_integration:
            Ke = KsReducedlocal(elementSize[0], elementSize[1], elementSize[2], subgrade_properties.Kx, subgrade_properties.Ky , subgrade_properties.Kz)
        else:
            Ke = Kslocal(elementSize[0], elementSize[1], elementSize[2], subgrade_properties.Kx, subgrade_properties.Ky , subgrade_properties.Kz)
        
        for i_loc, I in enumerate(dofs):
            for j_loc, J in enumerate(dofs):
                if True: #I >= J: #Since Kg is symmetric, store only the lower triangle. Python has no good way to solve this, so I am storing the whole thing here. Can be done better in Eigen!
                    Kg[I, J] += Ke[i_loc, j_loc]
    
    print('Success!')
    #spy(Kg); show()

    print("**** Assembling Global Force Vector (Fg)")
    Fg = np.zeros((mesh.getNumDofs(),))
    #Loop over each load
    for load in loads:
        #For the load, figure out which elements it overlaps with
        intersecting_elements = mesh.partitionRectangleToElements(load.rectangleBounds)

        #For each of those elements, figure out the overlapping bounds
        for elem in intersecting_elements:
            elementSize = mesh.getSizeOfElement(elem)
            localBoundsInElement = mesh.getLocalRectangleBoundsInElement(elem, load.rectangleBounds)
            
            #Create Fe and assemble into Fg
            Fe = Flocal(elementSize[0], elementSize[1], localBoundsInElement[0], localBoundsInElement[1], localBoundsInElement[2], localBoundsInElement[3], load.q)
            dofs = mesh.getDofsOfElement(elem)
            for i_loc, I in enumerate(dofs):
                Fg[I] += Fe[i_loc]
    print('Success!')

    print("**** Solving the system")
    Ug = spsolve(Kg.tocsr(), Fg)
    #print(f"Ug = {Ug.reshape(mesh.getNumNodes(), mesh.num_dofs_per_node)}")
    #print(f"Fg = {Fg.reshape(mesh.getNumNodes(), mesh.num_dofs_per_node)}")
    print('Success!')

    print("**** Post-processing: Calculating Nodal Stresses and Strains")
    #nodal_strain, nodal_stress = post_processor(mesh, slab_properties, Ug)
    print('Success!')

    print("**** Writing to file")
    filename = "output.vtk"
    #export_to_vtk(mesh, slab_properties, Ug, filename)
    export_to_csv(mesh, slab_properties, Ug, 'output-c3d8.csv')
    print('Success!')