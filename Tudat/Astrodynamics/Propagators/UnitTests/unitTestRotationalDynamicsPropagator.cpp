/*    Copyright (c) 2010-2018, Delft University of Technology
 *    All rigths reserved
 *
 *    This file is part of the Tudat. Redistribution and use in source and
 *    binary forms, with or without modification, are permitted exclusively
 *    under the terms of the Modified BSD license. You should have received
 *    a copy of the license with this file. If not, please or visit:
 *    http://tudat.tudelft.nl/LICENSE.
 */

#define BOOST_TEST_MAIN

#include <limits>
#include <string>
#include <thread>


#include <boost/make_shared.hpp>
#include <boost/test/unit_test.hpp>

#include <Eigen/Geometry>

#include "Tudat/Basics/testMacros.h"

#include "Tudat/Mathematics/BasicMathematics/linearAlgebra.h"
#include "Tudat/Astrodynamics/BasicAstrodynamics/physicalConstants.h"
#include "Tudat/Astrodynamics/BasicAstrodynamics/orbitalElementConversions.h"
#include "Tudat/Astrodynamics/Ephemerides/tabulatedEphemeris.h"

#include "Tudat/External/SpiceInterface/spiceInterface.h"
#include "Tudat/Mathematics/NumericalIntegrators/rungeKuttaCoefficients.h"
#include "Tudat/Astrodynamics/BasicAstrodynamics/accelerationModel.h"
#include "Tudat/InputOutput/basicInputOutput.h"

#include "Tudat/Astrodynamics/BasicAstrodynamics/sphericalStateConversions.h"
#include "Tudat/Astrodynamics/BasicAstrodynamics/torqueModelTypes.h"
#include "Tudat/External/SpiceInterface/spiceRotationalEphemeris.h"
#include "Tudat/Astrodynamics/Ephemerides/constantEphemeris.h"
#include "Tudat/Astrodynamics/Ephemerides/keplerEphemeris.h"
#include "Tudat/SimulationSetup/EnvironmentSetup/body.h"
#include "Tudat/Astrodynamics/Gravitation/centralGravityModel.h"
#include "Tudat/SimulationSetup/EnvironmentSetup/defaultBodies.h"
#include "Tudat/SimulationSetup/EnvironmentSetup/createBodies.h"
#include "Tudat/SimulationSetup/PropagationSetup/createNumericalSimulator.h"
#include "Tudat/Mathematics/NumericalIntegrators/createNumericalIntegrator.h"
#include "Tudat/SimulationSetup/PropagationSetup/propagationSettings.h"
#include "Tudat/SimulationSetup/PropagationSetup/dynamicsSimulator.h"
#include "Tudat/Astrodynamics/Aerodynamics/UnitTests/testApolloCapsuleCoefficients.h"

namespace tudat
{
namespace unit_tests
{

//Using declarations.
using namespace tudat::ephemerides;
using namespace tudat::interpolators;
using namespace tudat::numerical_integrators;
using namespace tudat::spice_interface;
using namespace tudat::simulation_setup;
using namespace tudat::basic_astrodynamics;
using namespace tudat::orbital_element_conversions;
using namespace tudat::propagators;
using namespace tudat::coordinate_conversions;
using namespace tudat::reference_frames;

// Create body map to be used for propagation of rotational motion of Phobos (no coupling to orbit)
NamedBodyMap getTestBodyMap( const double phobosSemiMajorAxis,
                             const bool useSymmetricEquator = 0 )
{
    NamedBodyMap bodyMap;
    bodyMap[ "Mars" ] = boost::make_shared< Body >( );
    bodyMap[ "Mars" ]->setEphemeris( boost::make_shared< ephemerides::ConstantEphemeris >(
                                         boost::lambda::constant( Eigen::Vector6d::Zero( ) ) ) );
    bodyMap[ "Phobos" ] = boost::make_shared< Body >( );

    Eigen::Matrix3d phobosInertiaTensor = Eigen::Matrix3d::Zero( );
    phobosInertiaTensor( 0, 0 ) = 0.3615;
    phobosInertiaTensor( 1, 1 ) = 0.4265;
    phobosInertiaTensor( 2, 2 ) = 0.5024;

    if( useSymmetricEquator )
    {
        phobosInertiaTensor( 0, 0 ) = phobosInertiaTensor( 1, 1 );
    }

    phobosInertiaTensor *= (11.27E3 * 11.27E3 * 1.0659E16 );
    bodyMap[ "Phobos" ]->setBodyInertiaTensor( phobosInertiaTensor );

    Eigen::Quaterniond noRotationQuaternion = Eigen::Quaterniond( Eigen::Matrix3d::Identity( ) );
    Eigen::Matrix< double, 7, 1 > unitRotationState = Eigen::Matrix< double, 7, 1 >::Zero( );
    unitRotationState( 0 ) = noRotationQuaternion.w( );
    unitRotationState( 1 ) = noRotationQuaternion.x( );
    unitRotationState( 2 ) = noRotationQuaternion.y( );
    unitRotationState( 3 ) = noRotationQuaternion.z( );

    std::map< double, Eigen::Matrix< double, 7, 1 > > dummyRotationMap;
    dummyRotationMap[ -1.0E100 ] = unitRotationState;
    dummyRotationMap[ 1.0E100 ] = unitRotationState;

    boost::shared_ptr< interpolators::OneDimensionalInterpolator< double, Eigen::Matrix< double, 7, 1 > > > dummyInterpolator =
            boost::make_shared< interpolators::LinearInterpolator< double, Eigen::Matrix< double, 7, 1 > > >( dummyRotationMap );
    bodyMap[ "Phobos" ]->setRotationalEphemeris( boost::make_shared< TabulatedRotationalEphemeris< double, double > >(
                                                     dummyInterpolator, "ECLIPJ2000", "Phobos_Fixed" ) );


    return bodyMap;
}

BOOST_AUTO_TEST_SUITE( test_rotational_dynamics_propagation )

//! Function to test torque-free propagation with initial rotation around one of its principal axes
BOOST_AUTO_TEST_CASE( testSimpleRotationalDynamicsPropagation )
{
    //Load spice kernels.
    spice_interface::loadStandardSpiceKernels( );

    // Perform test for initial rotationa about body-fixed x, y and z axes.
    for( unsigned axisCase = 0; axisCase < 3; axisCase++ )
    {
        // Retrieve list of body objects.
        NamedBodyMap bodyMap = getTestBodyMap( 9376.0E3 );

        // Define time range of test.
        double initialEphemerisTime = 1.0E7;
        double finalEphemerisTime = initialEphemerisTime + 10.0 * 86400.0;

        // Set torques between bodies that are to be taken into account.
        SelectedTorqueMap torqueMap;
        std::vector< std::string > bodiesToIntegrate;
        bodiesToIntegrate.push_back( "Phobos" );

        // Define mean motion (equal to rotation rate).
        double phobosSemiMajorAxis = 9376.0E3;
        double meanMotion = std::sqrt( getBodyGravitationalParameter( "Mars" ) / std::pow( phobosSemiMajorAxis, 3.0 ) );

        // Define initial rotational state
        Eigen::Quaterniond initialRotation =
                reference_frames::getRotatingPlanetocentricToLocalVerticalFrameTransformationQuaternion(
                    0.2, 0.7 );
        Eigen::Matrix3d initialRotationMatrixToBaseFrame = initialRotation.toRotationMatrix( );
        Eigen::Matrix3d initialRotationMatrixToTargetFrame = initialRotationMatrixToBaseFrame.transpose( );
        Eigen::VectorXd systemInitialState = Eigen::VectorXd::Zero( 7 );
        systemInitialState.segment( 0, 4 ) = linear_algebra::convertQuaternionToVectorFormat(
                    initialRotation );
        systemInitialState( 4 + axisCase ) = meanMotion;

        // Create torque models
        basic_astrodynamics::TorqueModelMap torqueModelMap = createTorqueModelsMap(
                    bodyMap, torqueMap );

        // Define propagator settings.
        boost::shared_ptr< RotationalStatePropagatorSettings< double > > propagatorSettings =
                boost::make_shared< RotationalStatePropagatorSettings< double > >
                ( torqueModelMap, bodiesToIntegrate, systemInitialState, boost::make_shared< PropagationTimeTerminationSettings >(
                      finalEphemerisTime ) );

        // Define integrator settings.
        boost::shared_ptr< numerical_integrators::IntegratorSettings< > > integratorSettings =
                boost::make_shared< RungeKuttaVariableStepSizeSettings< > >
                ( rungeKuttaVariableStepSize,
                  initialEphemerisTime, 10.0,
                  RungeKuttaCoefficients::rungeKuttaFehlberg78,
                  2.0, 30.0, 1.0E-13, 1.0E-13 );



        // Propagate dynamics
        SingleArcDynamicsSimulator< double > dynamicsSimulator(
                    bodyMap, integratorSettings, propagatorSettings, true, false, true );


        // Retrieve Phobos rotation model with reset rotational state
        boost::shared_ptr< RotationalEphemeris > phobosRotationalEphemeris = bodyMap[ "Phobos" ]->getRotationalEphemeris( );

        // Declare rotational velocity vectors to compute/expect
        Eigen::Vector3d currentRotationalVelocityInTargetFrame, currentRotationalVelocityInBaseFrame;
        Eigen::Vector3d expectedRotationalVelocityVectorInBaseFrame;

        // Declare rotation rate in body-fixed frame (constant)
        Eigen::Vector3d expectedRotationalVelocityVectorInTargetFrame = Eigen::Vector3d::Zero( );
        expectedRotationalVelocityVectorInTargetFrame( axisCase ) = meanMotion;

        // Declare rotatio matrices to compute/expect
        Eigen::Matrix3d currentRotationMatrixToTargetFrame, currentRotationMatrixToBaseFrame;
        Eigen::Matrix3d expectedRotationMatrixToTargetFrame, expectedRotationMatrixToBaseFrame;

        // Declare rotatio matrix derivatives to compute/expect
        Eigen::Matrix3d currentRotationMatrixDerivativeToTargetFrame, currentRotationMatrixDerivativeToBaseFrame,
                currentIndirectRotationMatrixDerivative;
        Eigen::Matrix3d expectedRotationMatrixDerivativeToTargetFrame, expectedRotationMatrixDerivativeToBaseFrame;

        // Declare expected rotation matrices w.r.t. initial rotational state
        Eigen::Matrix3d expectedRotationToTargetFrameFromInitialRotation, expectedRotationToBaseFrameFromInitialRotation;

        // Compare expected and true rotational state for list of times
        double startTime = initialEphemerisTime;
        double endTime = finalEphemerisTime - 3600.0;
        double currentTime = startTime;
        double timeStep = 600.0;
        while ( currentTime < endTime )
        {
            // Define expected rotation angle
            double currentAngle = meanMotion * ( currentTime - initialEphemerisTime );

            // Compute expected rotation matrices and compare to result from ephemerides
            Eigen::Vector3d rotationAxis;
            if( axisCase == 0 )
            {
                rotationAxis = Eigen::Vector3d::UnitX( );
            }
            else if( axisCase == 1 )
            {
                rotationAxis = Eigen::Vector3d::UnitY( );
            }
            else if( axisCase == 2 )
            {
                rotationAxis = Eigen::Vector3d::UnitZ( );
            }

            Eigen::Matrix3d baseRotationToTargetFrame =
                    Eigen::AngleAxisd( -1.0 * currentAngle, rotationAxis ).toRotationMatrix( );

            currentRotationMatrixToTargetFrame = phobosRotationalEphemeris->getRotationToTargetFrame( currentTime );
            expectedRotationToTargetFrameFromInitialRotation =
                    baseRotationToTargetFrame * initialRotationMatrixToTargetFrame;
            for( unsigned int i = 0; i < 3; i++ )
            {
                for( unsigned int j = 0; j < 3; j++ )
                {
                    BOOST_CHECK_SMALL(
                                std::fabs( currentRotationMatrixToTargetFrame( i, j ) -
                                           expectedRotationToTargetFrameFromInitialRotation( i, j ) ), 1.0E-10 );
                }
            }

            currentRotationMatrixToBaseFrame = phobosRotationalEphemeris->getRotationToBaseFrame( currentTime );
            expectedRotationToBaseFrameFromInitialRotation =
                    initialRotationMatrixToBaseFrame * baseRotationToTargetFrame.transpose( );
            for( unsigned int i = 0; i < 3; i++ )
            {
                for( unsigned int j = 0; j < 3; j++ )
                {
                    BOOST_CHECK_SMALL(
                                std::fabs( currentRotationMatrixToBaseFrame( i, j ) -
                                           expectedRotationToBaseFrameFromInitialRotation( i, j ) ), 1.0E-10 );
                }
            }

            // Compute expected rotation matrix derivatives and compare to result from ephemerides
            currentRotationMatrixDerivativeToTargetFrame =
                    phobosRotationalEphemeris->getDerivativeOfRotationToTargetFrame( currentTime );
            Eigen::Matrix3d premultiplierMatrix;

            if( axisCase == 0 )
            {
                premultiplierMatrix = reference_frames::X_AXIS_ROTATION_MATRIX_DERIVATIVE_PREMULTIPLIER;
            }
            else if( axisCase == 1 )
            {
                premultiplierMatrix = reference_frames::Y_AXIS_ROTATION_MATRIX_DERIVATIVE_PREMULTIPLIER ;
            }
            else if( axisCase == 2 )
            {
                premultiplierMatrix = reference_frames::Z_AXIS_ROTATION_MATRIX_DERIVATIVE_PREMULTIPLIER ;
            }

            expectedRotationMatrixDerivativeToTargetFrame =
                    meanMotion * premultiplierMatrix * baseRotationToTargetFrame *
                    initialRotationMatrixToTargetFrame;

            for( unsigned int i = 0; i < 3; i++ )
            {
                for( unsigned int j = 0; j < 3; j++ )
                {
                    BOOST_CHECK_SMALL(
                                std::fabs( currentRotationMatrixDerivativeToTargetFrame( i, j ) -
                                           expectedRotationMatrixDerivativeToTargetFrame( i, j ) ), meanMotion * 1.0E-10 );
                }
            }

            currentRotationMatrixDerivativeToBaseFrame =
                    phobosRotationalEphemeris->getDerivativeOfRotationToBaseFrame( currentTime );
            expectedRotationMatrixDerivativeToBaseFrame =
                    expectedRotationMatrixDerivativeToTargetFrame.transpose( );

            for( unsigned int i = 0; i < 3; i++ )
            {
                for( unsigned int j = 0; j < 3; j++ )
                {
                    BOOST_CHECK_SMALL(
                                std::fabs( currentRotationMatrixDerivativeToBaseFrame( i, j ) -
                                           expectedRotationMatrixDerivativeToBaseFrame( i, j ) ), meanMotion * 1.0E-10 );
                }
            }

            // Compute expected angular velocity vectors and compare to result from ephemerides
            currentRotationalVelocityInTargetFrame =
                    phobosRotationalEphemeris->getRotationalVelocityVectorInTargetFrame( currentTime );
            BOOST_CHECK_SMALL( std::fabs( currentRotationalVelocityInTargetFrame( 0 ) -
                                          expectedRotationalVelocityVectorInTargetFrame( 0 ) ),
                               meanMotion * 1.0E-15 );
            BOOST_CHECK_SMALL( std::fabs( currentRotationalVelocityInTargetFrame( 1 ) -
                                          expectedRotationalVelocityVectorInTargetFrame( 1 ) ),
                               meanMotion * 1.0E-15 );
            BOOST_CHECK_SMALL( std::fabs( currentRotationalVelocityInTargetFrame( 2 ) -
                                          expectedRotationalVelocityVectorInTargetFrame( 2 ) ),
                               meanMotion * 1.0E-15 );

            currentRotationalVelocityInBaseFrame =
                    phobosRotationalEphemeris->getRotationalVelocityVectorInBaseFrame( currentTime );
            expectedRotationalVelocityVectorInBaseFrame =
                    currentRotationMatrixToBaseFrame * expectedRotationalVelocityVectorInTargetFrame;
            BOOST_CHECK_SMALL( std::fabs( currentRotationalVelocityInBaseFrame( 0 ) -
                                          expectedRotationalVelocityVectorInBaseFrame( 0 ) ),
                               meanMotion * 1.0E-15 );
            BOOST_CHECK_SMALL( std::fabs( currentRotationalVelocityInBaseFrame( 1 ) -
                                          expectedRotationalVelocityVectorInBaseFrame( 1 ) ),
                               meanMotion * 1.0E-15 );
            BOOST_CHECK_SMALL( std::fabs( currentRotationalVelocityInBaseFrame( 2 ) -
                                          expectedRotationalVelocityVectorInBaseFrame( 2 ) ),
                               meanMotion * 1.0E-15 );

            currentTime += timeStep;
        }

        Eigen::Matrix3d numericalRotationMatrixDerivativeToBaseFrame, numericalRotationMatrixDerivativeToTargetFrame;
        Eigen::Matrix3d upperturbedMatrix, downperturbedMatrix;

        // Test whether rotation matrix derivatives are consistent with rotation matrices (using central differences)
        double timePerturbation = 0.1;
        currentTime = startTime + timeStep;
        while ( currentTime < endTime )
        {
            // Test rotation matrix derivative to base frame
            currentRotationMatrixDerivativeToBaseFrame =
                    phobosRotationalEphemeris->getDerivativeOfRotationToBaseFrame( currentTime );

            upperturbedMatrix =
                    phobosRotationalEphemeris->getRotationToBaseFrame( currentTime + timePerturbation ).toRotationMatrix( );
            downperturbedMatrix =
                    phobosRotationalEphemeris->getRotationToBaseFrame( currentTime - timePerturbation ).toRotationMatrix( );
            numericalRotationMatrixDerivativeToBaseFrame =
                    ( upperturbedMatrix - downperturbedMatrix ) / ( 2.0 * timePerturbation );

            for( unsigned int i = 0; i < 3; i++ )
            {
                for( unsigned int j = 0; j < 3; j++ )
                {
                    BOOST_CHECK_SMALL(
                                std::fabs( numericalRotationMatrixDerivativeToBaseFrame( i, j ) -
                                           currentRotationMatrixDerivativeToBaseFrame( i, j ) ), 1.0E-12 );
                }
            }

            // Test rotation matrix derivative to target frame
            currentRotationMatrixDerivativeToTargetFrame =
                    phobosRotationalEphemeris->getDerivativeOfRotationToTargetFrame( currentTime );

            upperturbedMatrix =
                    phobosRotationalEphemeris->getRotationToTargetFrame( currentTime + timePerturbation ).toRotationMatrix( );
            downperturbedMatrix =
                    phobosRotationalEphemeris->getRotationToTargetFrame( currentTime - timePerturbation ).toRotationMatrix( );
            numericalRotationMatrixDerivativeToTargetFrame =
                    ( upperturbedMatrix - downperturbedMatrix ) / ( 2.0 * timePerturbation );

            for( unsigned int i = 0; i < 3; i++ )
            {
                for( unsigned int j = 0; j < 3; j++ )
                {
                    BOOST_CHECK_SMALL(
                                std::fabs( numericalRotationMatrixDerivativeToTargetFrame( i, j ) -
                                           currentRotationMatrixDerivativeToTargetFrame( i, j ) ), 1.0E-12 );
                }
            }

            currentTime += timeStep;

        }
    }
}

//! Function to test torque-free propagation with initial rotation not around one of its principal axes. The compuited results
//! are compared to the expected precession
BOOST_AUTO_TEST_CASE( testSimpleRotationalDynamicsPropagationWithObliquity )
{
    //Load spice kernels.
    spice_interface::loadStandardSpiceKernels( );

    // Retrieve list of body objects.
    NamedBodyMap bodyMap = getTestBodyMap( 9376.0E3, 1 );

    // Define time range of test.
    double initialEphemerisTime = 1.0E7;
    double finalEphemerisTime = initialEphemerisTime + 10.0 * 86400.0;

    // Set torques between bodies that are to be taken into account.
    SelectedTorqueMap torqueMap;
    std::vector< std::string > bodiesToIntegrate;
    bodiesToIntegrate.push_back( "Phobos" );

    // Define mean motion (equal to rotation rate).
    double phobosSemiMajorAxis = 9376.0E3;
    double meanMotion = std::sqrt( getBodyGravitationalParameter( "Mars" ) /
                                   std::pow( phobosSemiMajorAxis, 3.0 ) );

    // Define initial rotational state
    Eigen::Quaterniond nominalInitialRotation = Eigen::Quaterniond( 1.0, 0.0, 0.0, 0.0 );
    double initialObliquity = 20.0 * mathematical_constants::PI / 180.0;
    Eigen::VectorXd systemInitialState = Eigen::VectorXd::Zero( 7 );
    systemInitialState.segment( 0, 4 ) = linear_algebra::convertQuaternionToVectorFormat(
                Eigen::AngleAxisd( -initialObliquity, Eigen::Vector3d::UnitX( ) ) * nominalInitialRotation );
    double initialXAngularVelocity = 0.1 * meanMotion;
    systemInitialState( 4 ) = initialXAngularVelocity;
    systemInitialState( 5 ) = 0.0 * meanMotion;
    systemInitialState( 6 ) = meanMotion;

    // Create torque models
    basic_astrodynamics::TorqueModelMap torqueModelMap = createTorqueModelsMap(
                bodyMap, torqueMap );

    // Define integrator settings.
    boost::shared_ptr< numerical_integrators::IntegratorSettings< > > integratorSettings =
            boost::make_shared< RungeKuttaVariableStepSizeSettings< > >
            ( rungeKuttaVariableStepSize,
              initialEphemerisTime, 10.0,
              RungeKuttaCoefficients::rungeKuttaFehlberg78,
              30.0, 300.0, 1.0E-14, 1.0E-14 );

    // Define propagator settings.
    boost::shared_ptr< RotationalStatePropagatorSettings< double > > propagatorSettings =
            boost::make_shared< RotationalStatePropagatorSettings< double > >
            ( torqueModelMap, bodiesToIntegrate, systemInitialState, boost::make_shared< PropagationTimeTerminationSettings >(
                  finalEphemerisTime ) );

    // Propagate dynamics
    SingleArcDynamicsSimulator< double > dynamicsSimulator(
                bodyMap, integratorSettings, propagatorSettings, true, false, true );


    // Retrieve Phobos rotation model with reset rotational state
    boost::shared_ptr< RotationalEphemeris > phobosRotationalEphemeris = bodyMap[ "Phobos" ]->getRotationalEphemeris( );

    // Declare vectors/matrices to be used in test
    Eigen::Vector3d currentRotationalVelocityInBaseFrame, currentRotationalVelocityInTargetFrame,
            indirectRotationalVelocityInBaseFrame;
    Eigen::Matrix3d currentRotationMatrixToBaseFrame, currentRotationMatrixToTargetFrame;
    Eigen::Matrix3d currentRotationMatrixDerivativeToBaseFrame, currentRotationMatrixDerivativeToTargetFrame;
    Eigen::Matrix3d numericalRotationMatrixDerivativeToBaseFrame, numericalRotationMatrixDerivativeToTargetFrame;
    Eigen::Matrix3d upperturbedMatrix, downperturbedMatrix;

    //    Eigen::Vector3d eulerAngles, calculatedEulerAngleRates, expectedEulerAngleRates;
    //    double eulerPhase = 0.0;

    // Compare expected and true rotational state for list of times
    double startTime = initialEphemerisTime + 3600.0;
    double endTime = finalEphemerisTime - 3600.0;
    double currentTime = startTime;
    double timeStep = ( endTime - startTime ) / 20.0;
    double timePerturbation = 0.1;

    double eulerFrequency = ( 0.5024 - 0.4265 ) / 0.4265 * meanMotion;

    while ( currentTime < endTime )
    {
        currentRotationalVelocityInTargetFrame =
                phobosRotationalEphemeris->getRotationalVelocityVectorInTargetFrame( currentTime );

        // Compare propagated and expected angular velocity vecots
        BOOST_CHECK_SMALL( currentRotationalVelocityInTargetFrame( 0 ) -
                           initialXAngularVelocity * std::cos( eulerFrequency * ( currentTime - initialEphemerisTime ) ),
                           1.0E-15 );
        BOOST_CHECK_SMALL( currentRotationalVelocityInTargetFrame( 1 ) -
                           initialXAngularVelocity * std::sin( eulerFrequency * ( currentTime - initialEphemerisTime ) ),
                           1.0E-15 );
        BOOST_CHECK_CLOSE_FRACTION( currentRotationalVelocityInTargetFrame( 2 ), meanMotion, 1.0E-15 );

        // Compare rotation matrix derivative to base frame with finite difference result
        currentRotationMatrixDerivativeToBaseFrame =
                phobosRotationalEphemeris->getDerivativeOfRotationToBaseFrame( currentTime );

        upperturbedMatrix =
                phobosRotationalEphemeris->getRotationToBaseFrame( currentTime + timePerturbation ).toRotationMatrix( );
        downperturbedMatrix =
                phobosRotationalEphemeris->getRotationToBaseFrame( currentTime - timePerturbation ).toRotationMatrix( );
        numericalRotationMatrixDerivativeToBaseFrame =
                ( upperturbedMatrix - downperturbedMatrix ) / ( 2.0 * timePerturbation );

        for( unsigned int i = 0; i < 3; i++ )
        {
            for( unsigned int j = 0; j < 3; j++ )
            {
                BOOST_CHECK_SMALL(
                            std::fabs( numericalRotationMatrixDerivativeToBaseFrame( i, j ) -
                                       currentRotationMatrixDerivativeToBaseFrame( i, j ) ), 1.0E-12 );
            }
        }

        // Compare rotation matrix derivative to target frame with finite difference result
        currentRotationMatrixDerivativeToTargetFrame =
                phobosRotationalEphemeris->getDerivativeOfRotationToTargetFrame( currentTime );

        upperturbedMatrix =
                phobosRotationalEphemeris->getRotationToTargetFrame( currentTime + timePerturbation ).toRotationMatrix( );
        downperturbedMatrix =
                phobosRotationalEphemeris->getRotationToTargetFrame( currentTime - timePerturbation ).toRotationMatrix( );
        numericalRotationMatrixDerivativeToTargetFrame =
                ( upperturbedMatrix - downperturbedMatrix ) / ( 2.0 * timePerturbation );

        for( unsigned int i = 0; i < 3; i++ )
        {
            for( unsigned int j = 0; j < 3; j++ )
            {
                BOOST_CHECK_SMALL(
                            std::fabs( numericalRotationMatrixDerivativeToTargetFrame( i, j ) -
                                       currentRotationMatrixDerivativeToTargetFrame( i, j ) ), 1.0E-12 );
            }
        }

        // Test consistency between rotation matrix and derivative with expected angular velocity vector
        indirectRotationalVelocityInBaseFrame =
                getRotationalVelocityVectorInBaseFrameFromMatrices(
                    phobosRotationalEphemeris->getRotationToTargetFrame( currentTime ).toRotationMatrix( ),
                    phobosRotationalEphemeris->getDerivativeOfRotationToBaseFrame( currentTime ) );

        currentRotationalVelocityInBaseFrame = phobosRotationalEphemeris->getRotationalVelocityVectorInBaseFrame( currentTime );
        for( unsigned int j = 0; j < 3; j++ )
        {
            BOOST_CHECK_SMALL(
                        std::fabs( indirectRotationalVelocityInBaseFrame( j ) -
                                   currentRotationalVelocityInBaseFrame( j ) ), 1.0E-15 );
        }

        currentRotationalVelocityInBaseFrame =
                phobosRotationalEphemeris->getRotationToBaseFrame( currentTime ) * currentRotationalVelocityInTargetFrame;
        for( unsigned int j = 0; j < 3; j++ )
        {
            BOOST_CHECK_SMALL(
                        std::fabs( indirectRotationalVelocityInBaseFrame( j ) -
                                   currentRotationalVelocityInBaseFrame( j ) ), 1.0E-15 );
        }

        currentTime += timeStep;
    }
}

//! Perform concurrent rotational and translational dynamics, with aerodynamic force and torque-free rotational motion, and
//! check if aerodynamic angles and force coefficients are indeed taken from propagated rotation.
BOOST_AUTO_TEST_CASE( testRotationalAndTranslationalDynamicsPropagation )
{

    // Load Spice kernels.
    spice_interface::loadStandardSpiceKernels( );

    // Set simulation start epoch.
    const double simulationStartEpoch = 0.0;

    // Set simulation end epoch.
    const double simulationEndEpoch = 3100.0;

    // Set numerical integration fixed step size.
    const double fixedStepSize = 1.0;

    // Define simulation body settings.
    std::map< std::string, boost::shared_ptr< BodySettings > > bodySettings =
            getDefaultBodySettings( { "Earth" }, simulationStartEpoch - 10.0 * fixedStepSize,
                                    simulationEndEpoch + 10.0 * fixedStepSize );
    bodySettings[ "Earth" ]->ephemerisSettings = boost::make_shared< simulation_setup::ConstantEphemerisSettings >(
                Eigen::Vector6d::Zero( ), "SSB", "J2000" );
    bodySettings[ "Earth" ]->rotationModelSettings->resetOriginalFrame( "J2000" );

    // Create Earth object
    simulation_setup::NamedBodyMap bodyMap = simulation_setup::createBodies( bodySettings );

    // Create vehicle objects.
    bodyMap[ "Apollo" ] = boost::make_shared< simulation_setup::Body >( );
    bodyMap[ "Apollo" ]->setConstantBodyMass( 5.0E3 );

    // Create vehicle aerodynamic coefficients
    bodyMap[ "Apollo" ]->setAerodynamicCoefficientInterface(
                unit_tests::getApolloCoefficientInterface( ) );

    // Set inertia tensor (dummy values)
    Eigen::Matrix3d inertiaTensor = Eigen::Matrix3d::Zero( );
    inertiaTensor( 0, 0 ) = 0.3615;
    inertiaTensor( 1, 1 ) = 0.4265;
    inertiaTensor( 2, 2 ) = 0.5024;
    inertiaTensor *= ( 0.1 * 25.0 * 5.0E3 );
    bodyMap[ "Apollo" ]->setBodyInertiaTensor( inertiaTensor );

    std::map< double, Eigen::Matrix< double, 7, 1 > > dummyRotationMap;
    dummyRotationMap[ -1.0E100 ] = Eigen::Matrix< double, 7, 1 >::Zero( );
    dummyRotationMap[ 1.0E100 ] = Eigen::Matrix< double, 7, 1 >::Zero( );

    // Set tabulated ephemerides for orbit and rotation
    boost::shared_ptr< interpolators::OneDimensionalInterpolator< double, Eigen::Matrix< double, 7, 1 > > >
            dummyRotationInterpolator =
            boost::make_shared< interpolators::LinearInterpolator< double, Eigen::Matrix< double, 7, 1 > > >( dummyRotationMap );
    bodyMap[ "Apollo" ]->setRotationalEphemeris( boost::make_shared< TabulatedRotationalEphemeris< double, double > >(
                                                     dummyRotationInterpolator, "J2000", "Apollo_Fixed" ) );

    std::map< double, Eigen::Matrix< double, 6, 1 > > dummyStateMap;
    dummyStateMap[ -1.0E100 ] = Eigen::Matrix< double, 6, 1 >::Zero( );
    dummyStateMap[ 1.0E100 ] = Eigen::Matrix< double, 6, 1 >::Zero( );
    boost::shared_ptr< interpolators::OneDimensionalInterpolator< double, Eigen::Matrix< double, 6, 1 > > >
            dummyStateInterpolator =
            boost::make_shared< interpolators::LinearInterpolator< double, Eigen::Matrix< double, 6, 1 > > >( dummyStateMap );
    bodyMap[ "Apollo" ]->setEphemeris( boost::make_shared< TabulatedCartesianEphemeris< double, double > >(
                                           dummyStateInterpolator, "SSB", "J2000" ) );

    // Finalize body creation.
    setGlobalFrameBodyEphemerides( bodyMap, "SSB", "J2000" );

    // Test simulation with and without torques.
    for( int simulationCase = 0; simulationCase < 2; simulationCase++ )
    {
        // Define propagator settings variables.
        SelectedAccelerationMap accelerationMap;
        std::vector< std::string > bodiesToPropagate;
        std::vector< std::string > centralBodies;

        // Define acceleration model settings.
        std::map< std::string, std::vector< boost::shared_ptr< AccelerationSettings > > > accelerationsOfApollo;
        accelerationsOfApollo[ "Earth" ].push_back( boost::make_shared< SphericalHarmonicAccelerationSettings >( 4, 0 ) );
        accelerationsOfApollo[ "Earth" ].push_back( boost::make_shared< AccelerationSettings >( aerodynamic ) );
        accelerationMap[  "Apollo" ] = accelerationsOfApollo;

        bodiesToPropagate.push_back( "Apollo" );
        centralBodies.push_back( "Earth" );

        // Create acceleration models
        basic_astrodynamics::AccelerationMap accelerationModelMap = createAccelerationModelsMap(
                    bodyMap, accelerationMap, bodiesToPropagate, centralBodies );

        // Set spherical elements for Apollo.
        Eigen::Vector6d apolloSphericalEntryState;
        apolloSphericalEntryState( SphericalOrbitalStateElementIndices::radiusIndex ) =
                spice_interface::getAverageRadius( "Earth" ) + 120.0E3;
        apolloSphericalEntryState( SphericalOrbitalStateElementIndices::latitudeIndex ) = 0.0;
        apolloSphericalEntryState( SphericalOrbitalStateElementIndices::longitudeIndex ) = 1.2;
        apolloSphericalEntryState( SphericalOrbitalStateElementIndices::speedIndex ) = 7.4E3;
        apolloSphericalEntryState( SphericalOrbitalStateElementIndices::flightPathIndex ) =
                -1.2 * mathematical_constants::PI / 180.0;
        apolloSphericalEntryState( SphericalOrbitalStateElementIndices::headingAngleIndex ) = 0.6;

        // Convert apollo state from spherical elements to Cartesian elements.
        Eigen::Vector6d systemInitialState = convertSphericalOrbitalToCartesianState(
                    apolloSphericalEntryState );
        boost::shared_ptr< ephemerides::RotationalEphemeris > earthRotationalEphemeris =
                bodyMap.at( "Earth" )->getRotationalEphemeris( );
        systemInitialState = transformStateToGlobalFrame( systemInitialState, simulationStartEpoch, earthRotationalEphemeris );


        // Define initial rotational state
        Eigen::Quaterniond initialRotation = Eigen::Quaterniond( Eigen::Matrix3d::Identity( ) );
        Eigen::VectorXd systemInitialRotationalState = Eigen::VectorXd::Zero( 7 );
        systemInitialRotationalState.segment( 0, 4 ) = linear_algebra::convertQuaternionToVectorFormat(
                    initialRotation );
        systemInitialRotationalState( 4 ) = 1.0E-4;

        // Create torque models
        SelectedTorqueMap selectedTorqueModelMap;
        if( simulationCase > 0 )
        {
            selectedTorqueModelMap[ "Apollo" ][ "Earth" ].push_back(
                        boost::make_shared< TorqueSettings >( aerodynamic_torque ) );
        }

        basic_astrodynamics::TorqueModelMap torqueModelMap = createTorqueModelsMap(
                    bodyMap, selectedTorqueModelMap );

        // Define list of dependent variables to save.
        std::vector< boost::shared_ptr< SingleDependentVariableSaveSettings > > dependentVariablesList;
        dependentVariablesList.push_back(
                    boost::make_shared< BodyAerodynamicAngleVariableSaveSettings >(
                        "Apollo", reference_frames::latitude_angle ) );
        dependentVariablesList.push_back(
                    boost::make_shared< BodyAerodynamicAngleVariableSaveSettings >(
                        "Apollo", reference_frames::longitude_angle ) );
        dependentVariablesList.push_back(
                    boost::make_shared< BodyAerodynamicAngleVariableSaveSettings >(
                        "Apollo", reference_frames::heading_angle ) );
        dependentVariablesList.push_back(
                    boost::make_shared< BodyAerodynamicAngleVariableSaveSettings >(
                        "Apollo", reference_frames::flight_path_angle ) );
        dependentVariablesList.push_back(
                    boost::make_shared< BodyAerodynamicAngleVariableSaveSettings >(
                        "Apollo", reference_frames::angle_of_attack ) );
        dependentVariablesList.push_back(
                    boost::make_shared< BodyAerodynamicAngleVariableSaveSettings >(
                        "Apollo", reference_frames::angle_of_sideslip ) );
        dependentVariablesList.push_back(
                    boost::make_shared< BodyAerodynamicAngleVariableSaveSettings >(
                        "Apollo", reference_frames::bank_angle   ) );
        if( simulationCase == 1 )
        {
            dependentVariablesList.push_back(
                        boost::make_shared< SingleDependentVariableSaveSettings >(
                            aerodynamic_force_coefficients_dependent_variable, "Apollo" ) );
            dependentVariablesList.push_back(
                        boost::make_shared< SingleTorqueDependentVariableSaveSettings >(
                            aerodynamic_torque, "Apollo", "Earth" ) );
            dependentVariablesList.push_back(
                        boost::make_shared< SingleDependentVariableSaveSettings >(
                            total_torque_dependent_variable, "Apollo" ) );
        }

        // Create object with list of dependent variables
        boost::shared_ptr< DependentVariableSaveSettings > dependentVariablesToSave =
                boost::make_shared< DependentVariableSaveSettings >( dependentVariablesList );

        // Define termination conditions
        boost::shared_ptr< PropagationTerminationSettings > terminationSettings =
                boost::make_shared< PropagationTimeTerminationSettings >( 250.0 );

        // Create propagator settings for rotation.
        boost::shared_ptr< RotationalStatePropagatorSettings< double > > rotationalPropagatorSettings =
                boost::make_shared< RotationalStatePropagatorSettings< double > >
                ( torqueModelMap, bodiesToPropagate, systemInitialRotationalState, terminationSettings );

        // Create propagation settings for translational dynamics.
        boost::shared_ptr< TranslationalStatePropagatorSettings< double > > translationalPropagatorSettings =
                boost::make_shared< TranslationalStatePropagatorSettings< double > >
                ( centralBodies, accelerationModelMap, bodiesToPropagate, systemInitialState,
                  terminationSettings, cowell );

        // Create full propagator settings for rotation.
        std::vector< boost::shared_ptr< SingleArcPropagatorSettings< double > > >  propagatorSettingsList;
        propagatorSettingsList.push_back( translationalPropagatorSettings );
        propagatorSettingsList.push_back( rotationalPropagatorSettings );

        boost::shared_ptr< PropagatorSettings< double > > propagatorSettings =
                boost::make_shared< MultiTypePropagatorSettings< double > >(
                    propagatorSettingsList, terminationSettings, dependentVariablesToSave );


        // Create integrator settings for rotation.
        boost::shared_ptr< IntegratorSettings< > > integratorSettings =
                boost::make_shared< RungeKuttaVariableStepSizeSettings< > >
                ( rungeKuttaVariableStepSize, 0.0, 0.02,
                  RungeKuttaCoefficients::rungeKuttaFehlberg78, 1.0E-4, 0.02, 1.0E-12, 1.0E-12 );

        // Create simulation object and propagate dynamics.
        SingleArcDynamicsSimulator< > dynamicsSimulator(
                    bodyMap, integratorSettings, propagatorSettings, true, false, true);
        std::map< double, Eigen::VectorXd > dependentVariableHistory = dynamicsSimulator.getDependentVariableHistory( );
        std::map< double, Eigen::VectorXd > propagationHistory = dynamicsSimulator.getEquationsOfMotionNumericalSolution( );

        // Define test variables
        double currentLatitude, currentLongitude, currentFlightPathAngle, currentHeadingAngle,
                currentAngleOfAttack, currentSideslipAngle, currentBankAngle, currentRotationAngle;
        Eigen::Matrix3d currentInertialToBodyFixedFrameRotation, currentEarthFixedToLVLHFrameRotation,
                currentLVLHToTrajectoryFrameRotation,
                currentTrajectoryToAerodynamicFrameRotation, currentAerodynamicToBodyFixesFrameRotation,
                currentInertialToBodyFixedFrame, expectedInertialToBodyFixedFrame;
        Eigen::Matrix3d expectedInertialToBodyFixedFrameRotation;
        boost::shared_ptr< RotationalEphemeris > earthRotationModel = bodyMap.at( "Earth" )->getRotationalEphemeris( );
        boost::shared_ptr< RotationalEphemeris > apolloRotationModel = bodyMap.at( "Apollo" )->getRotationalEphemeris( );

        // Iterate over saved data, manually compute inertial to body-fixed rotation, and compare to expected matrix for
        // simulationCase=0; test inertial time-derivative of angular momentum to check consistency with magnitude of torque
        std::map< double, Eigen::Vector3d > inertialAngularMomentumMap;
        std::map< double, Eigen::Vector3d > inertialTorqueMap;
        for( std::map< double, Eigen::VectorXd >::const_iterator variableIterator = dependentVariableHistory.begin( );
             variableIterator != dependentVariableHistory.end( ); variableIterator++ )
        {
            if( simulationCase == 0 )
            {
                // Retrieve saved angles
                currentLatitude = variableIterator->second( 0 );
                currentLongitude = variableIterator->second( 1 );
                currentHeadingAngle = variableIterator->second( 2 );
                currentFlightPathAngle = variableIterator->second( 3 );
                currentAngleOfAttack = variableIterator->second( 4 );
                currentSideslipAngle = variableIterator->second( 5 );
                currentBankAngle = variableIterator->second( 6 );

                // Compute matrices from angles
                currentInertialToBodyFixedFrameRotation = earthRotationModel->getRotationToTargetFrame( variableIterator->first );
                currentEarthFixedToLVLHFrameRotation = getRotatingPlanetocentricToLocalVerticalFrameTransformationQuaternion(
                            currentLongitude, currentLatitude );
                currentLVLHToTrajectoryFrameRotation = getLocalVerticalFrameToTrajectoryTransformationQuaternion(
                            currentFlightPathAngle, currentHeadingAngle );
                currentTrajectoryToAerodynamicFrameRotation = getTrajectoryToAerodynamicFrameTransformationQuaternion(
                            currentBankAngle );
                currentAerodynamicToBodyFixesFrameRotation = getAirspeedBasedAerodynamicToBodyFrameTransformationQuaternion(
                            currentAngleOfAttack, currentSideslipAngle );
                currentInertialToBodyFixedFrame = currentAerodynamicToBodyFixesFrameRotation *
                        currentTrajectoryToAerodynamicFrameRotation *
                        currentLVLHToTrajectoryFrameRotation * currentEarthFixedToLVLHFrameRotation *
                        currentInertialToBodyFixedFrameRotation;

                // Compure expected rotation angle and rotation matrix
                currentRotationAngle = systemInitialRotationalState( 4 ) * variableIterator->first;
                expectedInertialToBodyFixedFrame =
                        Eigen::AngleAxisd( -1.0 * currentRotationAngle, Eigen::Vector3d::UnitX( ) ).toRotationMatrix( );


                // Compare expected and actual rotation matrices
                for( unsigned int i = 0; i < 3; i++ )
                {
                    for( unsigned int j = 0; j < 3; j++ )
                    {
                        BOOST_CHECK_SMALL(
                                    std::fabs( expectedInertialToBodyFixedFrame( i, j ) -
                                               currentInertialToBodyFixedFrame( i, j ) ), 1.0E-13 );
                    }
                }
            }

            // Retrieve torque and angular momentum in inertial frame.
            else if( simulationCase == 1 )
            {
                Eigen::Matrix3d currentRotationFromApolloFixedToInertialFrame = apolloRotationModel->getRotationToBaseFrame(
                            variableIterator->first ).toRotationMatrix( );
                Eigen::Matrix3d currentRotationFromInertialToBodyFixedFrame =
                        currentRotationFromApolloFixedToInertialFrame.transpose( );
                Eigen::Matrix3d apolloInertiaTensorInInertialFrame =
                        currentRotationFromApolloFixedToInertialFrame * inertiaTensor *
                        currentRotationFromInertialToBodyFixedFrame;
                Eigen::Vector3d apolloAngularVelocityVectorInInertialFrame =
                        apolloRotationModel->getRotationalVelocityVectorInBaseFrame( variableIterator->first );
                inertialAngularMomentumMap[ variableIterator->first ] =
                        apolloInertiaTensorInInertialFrame * apolloAngularVelocityVectorInInertialFrame;
                inertialTorqueMap[ variableIterator->first ] =
                        currentRotationFromApolloFixedToInertialFrame * variableIterator->second.segment( 10, 3 );
            }
        }

        // Compare time rate of angular momentum (using finite differences) with torque magnitudes
        if( simulationCase == 1 )
        {
            // Create and set interpolator for angular momentum.
            boost::shared_ptr< interpolators::OneDimensionalInterpolator< double, Eigen::Vector3d > >
                    angularMomentumInterpolator =
                    boost::make_shared< interpolators::LagrangeInterpolator< double, Eigen::Vector3d > >(
                        inertialAngularMomentumMap, 6 );
            typedef interpolators::OneDimensionalInterpolator< double, Eigen::Vector3d > LocalInterpolator;
            boost::function< Eigen::Vector3d( const double ) > angularMomentumFunction = boost::bind(
                        static_cast< Eigen::Vector3d( LocalInterpolator::* )( const double ) >
                        ( &LocalInterpolator::interpolate ), angularMomentumInterpolator, _1 );

            double timeStep = 0.001;

            // Iterate over saved data, and check consistency of torque and angular momentum
            for( std::map< double, Eigen::Vector3d >::const_iterator variableIterator = ( inertialTorqueMap.begin( )++ );
                 variableIterator != inertialTorqueMap.end( ); variableIterator++ )
            {
                if( variableIterator->first < ( --inertialTorqueMap.end( ) )->first - 10.0
                        && variableIterator->first > inertialTorqueMap.begin( )->first + 10.0 )
                {
                    Eigen::Vector3d angularMomentumDerivative = numerical_derivatives::computeCentralDifference(
                                angularMomentumFunction, variableIterator->first, timeStep, numerical_derivatives::order4 );

                    for( unsigned int i = 0; i < 3; i++ )
                    {
                        BOOST_CHECK_SMALL(
                                    std::fabs( inertialTorqueMap.at( variableIterator->first )( i ) -
                                               angularMomentumDerivative( i ) ), 0.25 );
                    }
                }
            }
        }
    }
}

BOOST_AUTO_TEST_SUITE_END( )

}

}

