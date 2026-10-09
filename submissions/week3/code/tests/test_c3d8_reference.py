"""Source integrity, element conventions and captured C3D8 reference checks."""
import hashlib
import json
from pathlib import Path
import unittest
import numpy as np
from c3d8_reference_support import ROOT, element, definitions


class C3D8ReferenceTests(unittest.TestCase):
    def test_recorded_fixture_integrity_and_shapes(self):
        report=json.loads((ROOT/'docs/C3D8_BASELINE_MEASUREMENTS.json').read_text())
        self.assertEqual(report['status'],'Passed')
        self.assertEqual(len(report['runs']),5)
        for row in report['runs']:
            folder=ROOT/'tests/fixtures/c3d8'/row['case']
            for path,key in [(folder/'input.json','inputSha256'),(folder/'expected/reference.csv','csvSha256'),
                             (folder/'expected/arrays.npz','arraysSha256')]:
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),row[key],str(path))
            with np.load(folder/'expected/arrays.npz') as arrays:
                count=row['summary']['nodes']
                for name,components in [('coordinates',3),('displacement',3),('force',3),('stress',6),('strain',6)]:
                    self.assertEqual(arrays[name].shape,(count,components))
                    self.assertTrue(np.isfinite(arrays[name]).all())
                self.assertEqual(arrays['connectivity'].shape,(row['summary']['elements'],8))

    def test_received_sources_unchanged(self):
        manifest=json.loads((ROOT/'docs/C3D8_INTAKE_AUDIT.json').read_text())
        for name,record in manifest['files'].items():
            with self.subTest(file=name):
                self.assertEqual(hashlib.sha256((ROOT/name).read_bytes()).hexdigest(),record['sha256'])

    def test_mesh_enumeration_faces_and_patch_partition(self):
        mesh=definitions()['mesh_c3d8_enum_xzy'](np.array([10.,180.,460.]),np.array([-20.,260.]),np.array([5.,68.,151.]))
        expected=np.array([[x,y,z] for y in mesh.ynodes for z in mesh.znodes for x in mesh.xnodes])
        np.testing.assert_array_equal(mesh.getAllNodeCoordinates(),expected)
        self.assertEqual(mesh.getNumNodes(),18); self.assertEqual(mesh.getNumElements(),4)
        np.testing.assert_array_equal(mesh.getNodesOfElement(0),[0,1,10,9,3,4,13,12])
        np.testing.assert_array_equal(mesh.getDofsOfNode(5),[15,16,17])
        np.testing.assert_array_equal(mesh.getBottomElements(),[2,3])
        np.testing.assert_array_equal(mesh.partitionRectangleToElements(np.array([10,180,-20,260])),[0])
        np.testing.assert_array_equal(mesh.partitionRectangleToElements(np.array([30,330,10,220])),[0,1])
        np.testing.assert_array_equal(mesh.getLocalRectangleBoundsInElement(1,np.array([30,330,10,220])),[0,150,30,240])

    def test_full_stiffness_quadrature_and_rigid_modes(self):
        for a,b,c in ((175.,175.,50.),(170.,280.,63.),(1.,2.,3.)):
            K=element.Klocal(24000,.3,a,b,c); D=element.Dmat(24000,.3)
            integrated=np.zeros((24,24))
            for x in (-1/np.sqrt(3),1/np.sqrt(3)):
                for y in (-1/np.sqrt(3),1/np.sqrt(3)):
                    for z in (-1/np.sqrt(3),1/np.sqrt(3)):
                        B=element.Blocal(a*(x+1)/2,b*(y+1)/2,c*(z+1)/2,a,b,c)
                        integrated+=B.T@D@B*a*b*c/8
            np.testing.assert_allclose(K,integrated,atol=np.max(abs(K))*1e-13,rtol=1e-12)
            values=np.linalg.eigvalsh(K)
            self.assertEqual(np.count_nonzero(abs(values)>max(abs(values))*1e-10),18)
            self.assertGreaterEqual(min(values),-max(abs(values))*1e-10)
            supported=K+element.Kslocal(a,b,c,20,35,50)
            self.assertGreater(min(np.linalg.eigvalsh(supported)),0)

    def test_engineering_shear_and_load_force_moments(self):
        a,b,c=170.,280.,63.
        xyz=np.array([[0,0,0],[a,0,0],[a,b,0],[0,b,0],[0,0,c],[a,0,c],[a,b,c],[0,b,c]])
        U=np.column_stack((2*xyz[:,0]+3*xyz[:,1],5*xyz[:,1]+7*xyz[:,2],11*xyz[:,2]+13*xyz[:,0])).ravel()
        np.testing.assert_allclose(element.Blocal(31,82,29,a,b,c)@U,[2,5,11,3,7,13],atol=1e-12)
        F=element.Flocal(a,b,20,120,30,220,.8).reshape(-1,3)
        resultant=.8*100*190
        np.testing.assert_allclose(F.sum(axis=0),[0,0,resultant],atol=1e-9)
        np.testing.assert_allclose(np.cross(xyz,F).sum(axis=0),[125*resultant,-70*resultant,0],atol=1e-7)
        np.testing.assert_array_equal(F[4:],np.zeros((4,3)))

    def test_known_reduced_mode_defects_remain_explicit(self):
        # Document the received code's defects; these are not acceptance tests
        # for a supported reduced-integration production formulation.
        a,b,c=170.,280.,63.
        B=element.Blocal(a/2,b/2,c/2,a,b,c)
        Kr=element.KReducedlocal(24000,.3,a,b,c)
        expected=B.T@element.Dmat(24000,.3)@B*a*b*c
        np.testing.assert_allclose(8*Kr,expected,atol=1e-8,rtol=1e-12)
        Ks=element.KsReducedlocal(a,b,c,20,35,50)
        N=np.zeros((3,24))
        for n in range(4,8): N[:,3*n:3*n+3]=np.eye(3)/4
        np.testing.assert_allclose(4*Ks,N.T@np.diag([20,35,50])@N*a*b,atol=1e-8)
        spectrum=np.linalg.eigvalsh(Kr+Ks)
        self.assertEqual(np.count_nonzero(abs(spectrum)>max(abs(spectrum))*1e-10),9)

    def test_captured_analytical_compression_and_zero_load(self):
        fixture=ROOT/'tests/fixtures/c3d8'
        with np.load(fixture/'analytical_compression/expected/arrays.npz') as arrays:
            data=json.loads((fixture/'analytical_compression/input.json').read_text())
            q=data['loads'][0]['q']; E=data['material']['Emod']; kz=data['subgrade']['Kz']
            xyz=arrays['coordinates']; expected_w=q/kz+q*(xyz[:,2].max()-xyz[:,2])/E
            np.testing.assert_allclose(arrays['displacement'][:,2],expected_w,atol=1e-10,rtol=1e-9)
            np.testing.assert_allclose(arrays['displacement'][:,:2],0,atol=1e-10)
            stress=np.zeros_like(arrays['stress']); stress[:,2]=-q
            strain=np.zeros_like(arrays['strain']); strain[:,2]=-q/E
            np.testing.assert_allclose(arrays['stress'],stress,atol=1e-8)
            np.testing.assert_allclose(arrays['strain'],strain,atol=1e-12)
        with np.load(fixture/'zero_load/expected/arrays.npz') as arrays:
            for field in ('displacement','strain','stress','force'):
                np.testing.assert_array_equal(arrays[field],np.zeros_like(arrays[field]))


if __name__=='__main__':
    unittest.main()
